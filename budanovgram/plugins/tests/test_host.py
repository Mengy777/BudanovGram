"""Exercise the real native host, including its JavaScript execution budget."""
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import unittest

HOST = sys.argv.pop(1)


class Host:
    def __init__(self, script):
        self.process = subprocess.Popen([HOST], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            encoding='utf-8', bufsize=1)
        self.frames = queue.Queue()
        def reader():
            for line in self.process.stdout:
                self.frames.put(json.loads(line))
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()
        self.send({'type': 'load', 'manifest': {'id': 'test.plugin'},
            'script': script, 'settings': {'answer': 42}})

    def send(self, frame):
        self.process.stdin.write(json.dumps(frame) + '\n')
        self.process.stdin.flush()

    def until(self, predicate):
        while True:
            frame = self.frames.get(timeout=5)
            if predicate(frame):
                return frame

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=5)
        elif not self.process.stdin.closed:
            self.process.stdin.close()
        self.reader.join(timeout=2)
        self.process.stdout.close()
        self.process.stderr.close()


class RuntimeTests(unittest.TestCase):
    def host(self, script):
        host = Host(script)
        self.addCleanup(host.close)
        return host

    def test_arithmetic_and_settings(self):
        h = self.host("console.log(bg.settings.get('answer') * 2)")
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], '84')

    def test_command_context_and_async_response(self):
        h = self.host("bg.commands.register({id:'x',title:'X'},async c=>{"
            "const v=await bg.storage.get('key'); console.log(c.text+v);});")
        req = h.until(lambda f: f.get('method') == 'commands.register')
        h.send({'type': 'response', 'id': req['id'], 'result': True})
        h.send({'type': 'command', 'id': 'x', 'context': {'text': 'hello '}})
        req = h.until(lambda f: f.get('method') == 'storage.get')
        h.send({'type': 'response', 'id': req['id'], 'result': 'world'})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], 'hello world')

    def test_network_json(self):
        h = self.host("bg.network.fetch('https://example.org').then(async r=>console.log((await r.json()).x))")
        req = h.until(lambda f: f.get('method') == 'network.fetch')
        h.send({'type': 'response', 'id': req['id'], 'result': {'status': 200, 'body': '{"x":7}'}})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], '7')

    def test_host_error_becomes_rejected_promise(self):
        h = self.host("bg.clipboard.read().catch(e=>console.log(e.message))")
        req = h.until(lambda f: f.get('method') == 'clipboard.read')
        h.send({'type': 'response', 'id': req['id'], 'error': 'Denied'})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], 'Denied')

    def test_settings_event_updates_getter_before_callback(self):
        h = self.host("bg.settings.onChange(()=>console.log(bg.settings.get('answer')))")
        h.until(lambda f: f['type'] == 'ready')
        h.send({'type': 'event', 'name': 'settings.changed', 'data': {'answer': 99}})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], '99')

    def test_handler_error_does_not_destroy_other_handlers(self):
        h = self.host("bg.events.on('x',()=>{throw Error('bad')});bg.events.on('x',()=>console.log('good'))")
        h.until(lambda f: f['type'] == 'ready')
        h.send({'type': 'event', 'name': 'x', 'data': {}})
        self.assertEqual(h.until(lambda f: f['type'] == 'log' and f['text'] == 'good')['text'], 'good')

    def test_timer_callback(self):
        h = self.host("bg.timers.setTimeout(()=>console.log('tick'),100)")
        req = h.until(lambda f: f.get('method') == 'timers.start')
        h.send({'type': 'response', 'id': req['id'], 'result': True})
        h.send({'type': 'event', 'name': 'timer', 'data': {'id': req['params']['id']}})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], 'tick')

    def test_no_node_or_os_bridge(self):
        h = self.host("console.log(typeof process,typeof require,typeof std,typeof os,typeof __bgPost)")
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], ' '.join(['undefined']*5))

    def test_infinite_loop_is_interrupted(self):
        h = self.host("while(true){}")
        self.assertEqual(h.process.wait(timeout=5), 2)

    def test_infinite_microtasks_are_interrupted(self):
        h = self.host("function again(){Promise.resolve().then(again)}; again()")
        self.assertEqual(h.process.wait(timeout=5), 2)

    def test_memory_exhaustion_is_contained(self):
        h = self.host("const a=[];while(true)a.push(new Uint8Array(1024*1024));")
        self.assertEqual(h.process.wait(timeout=5), 2)

    def test_invalid_script_is_not_ready(self):
        h = self.host('invalid syntax !')
        self.assertEqual(h.process.wait(timeout=5), 2)

    def test_user_resolution_api(self):
        h = self.host("bg.users.resolve({account:'42'},'testuser').then(u=>console.log(u.id))")
        req = h.until(lambda f: f.get('method') == 'users.resolve')
        self.assertEqual(req['params'], {'account': '42', 'username': 'testuser'})
        h.send({'type': 'response', 'id': req['id'], 'result': {'id': '9007199254740993'}})
        self.assertEqual(h.until(lambda f: f['type'] == 'log')['text'], '9007199254740993')


if __name__ == '__main__':
    unittest.main()
