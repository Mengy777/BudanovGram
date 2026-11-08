(() => {
  'use strict';
  const post = globalThis.__bgPost;
  delete globalThis.__bgPost;
  const pending = new Map(), handlers = new Map(), commands = new Map();
  const timers = new Map();
  let sequence = 0, started = false, settings = {}, manifest = {};
  const send = value => post(JSON.stringify(value));
  const request = (method, params = {}) => {
    if (pending.size >= 128) return Promise.reject(new Error('Too many pending requests'));
    return new Promise((resolve, reject) => {
      const id = ++sequence;
      pending.set(id, {resolve, reject});
      send({type: 'request', id, method, params});
    });
  };
  const report = error => send({type: 'log', level: 'error', text: String(error?.stack || error)});
  const run = (fn, arg) => {
    try { Promise.resolve(fn(arg)).catch(report); } catch (error) { report(error); }
  };
  const log = level => (...args) => send({type: 'log', level,
    text: args.map(x => typeof x === 'string' ? x : JSON.stringify(x)).join(' ')});
  const events = Object.freeze({
    on(name, fn) {
      if (typeof fn !== 'function') throw new TypeError('Handler must be a function');
      if (!handlers.has(name)) handlers.set(name, new Set());
      handlers.get(name).add(fn);
      return () => handlers.get(name)?.delete(fn);
    },
    once(name, fn) {
      const off = events.on(name, value => { off(); return fn(value); });
      return off;
    }
  });
  const api = Object.freeze({
    apiVersion: 1,
    get manifest() { return JSON.parse(JSON.stringify(manifest)); },
    events,
    commands: Object.freeze({
      register({id, title, scope = 'message'}, handler) {
        if (!/^[a-zA-Z0-9._-]{1,80}$/.test(id) || typeof handler !== 'function')
          throw new TypeError('Invalid command');
        commands.set(id, handler);
        return request('commands.register', {id, title, scope});
      }
    }),
    settings: Object.freeze({
      get(key, fallback = null) { return Object.hasOwn(settings, key) ? settings[key] : fallback; },
      set(key, value) { return request('settings.set', {key, value}); },
      onChange(fn) { return events.on('settings.changed', fn); }
    }),
    storage: Object.freeze({
      get: (key, fallback = null) => request('storage.get', {key, fallback}),
      set: (key, value) => request('storage.set', {key, value}),
      remove: key => request('storage.remove', {key})
    }),
    network: Object.freeze({
      async fetch(url, options = {}) {
        const value = await request('network.fetch', {url, ...options});
        return Object.freeze({...value, ok: value.status >= 200 && value.status < 300,
          text: async () => value.body, json: async () => JSON.parse(value.body)});
      }
    }),
    clipboard: Object.freeze({
      read: () => request('clipboard.read'),
      write: text => request('clipboard.write', {text})
    }),
    ui: Object.freeze({
      notify: text => request('ui.notify', {text}),
      panel: panel => request('ui.panel', panel),
      openUrl: url => request('ui.openUrl', {url}),
      setLabels: labels => request('ui.labels', {labels}),
      setStyle: stylesheet => request('ui.style', {stylesheet})
    }),
    messages: Object.freeze({
      get: ref => request('messages.get', ref),
      setLocalText: (ref, text) => request('messages.setLocalText', {...ref, text}),
      resetLocalText: ref => request('messages.resetLocalText', ref),
      send: (ref, text) => request('messages.send', {...ref, text}),
      edit: (ref, text) => request('messages.edit', {...ref, text})
    }),
    resources: Object.freeze({read: name => request('resources.read', {name})}),
    client: Object.freeze({info: () => request('client.info')}),
    timers: Object.freeze({
      async setTimeout(fn, delay) {
        const id = String(++sequence);
        timers.set(id, {fn, repeat: false});
        try { await request('timers.start', {id, delay, repeat: false}); }
        catch (error) { timers.delete(id); throw error; }
        return id;
      },
      async setInterval(fn, delay) {
        const id = String(++sequence);
        timers.set(id, {fn, repeat: true});
        try { await request('timers.start', {id, delay, repeat: true}); }
        catch (error) { timers.delete(id); throw error; }
        return id;
      },
      clear(id) { timers.delete(id); return request('timers.stop', {id}); }
    }),
    log: log('info')
  });
  Object.defineProperty(globalThis, 'bg', {value: api});
  globalThis.console = Object.freeze({log: log('info'), info: log('info'), warn: log('warn'), error: log('error')});
  globalThis.__bgDispatch = line => {
    const frame = JSON.parse(line);
    if (frame.type === 'load') {
      if (started) throw new Error('Plugin already loaded');
      started = true;
      manifest = frame.manifest;
      settings = frame.settings || {};
      const activate = new Function('bg', '"use strict";\n' + frame.script);
      activate(api);
      send({type: 'ready'});
      for (const fn of handlers.get('plugin.start') || []) run(fn, {});
    } else if (frame.type === 'response') {
      const item = pending.get(frame.id);
      if (item) {
        pending.delete(frame.id);
        frame.error ? item.reject(new Error(frame.error)) : item.resolve(frame.result);
      }
    } else if (frame.type === 'event') {
      if (frame.name === 'settings.changed') settings = frame.data;
      if (frame.name === 'timer') {
        const timer = timers.get(frame.data.id);
        if (timer) {
          if (!timer.repeat) timers.delete(frame.data.id);
          run(timer.fn, undefined);
        }
      }
      for (const fn of handlers.get(frame.name) || []) run(fn, frame.data);
    } else if (frame.type === 'command') {
      const fn = commands.get(frame.id);
      if (fn) run(fn, frame.context);
    }
  };
})();
