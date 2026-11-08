#include "quickjs.h"
#include "sdk_source.h"

#include <chrono>
#include <iostream>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
auto Deadline = Clock::time_point();
constexpr auto MaxFrame = 2 * 1024 * 1024;

int Interrupted(JSContext *, void *) {
	return Clock::now() > Deadline;
}

JSValue Post(JSContext *context, JSValueConst, int count, JSValueConst *args) {
	if (count != 1) {
		return JS_ThrowTypeError(context, "post expects one argument");
	}
	size_t length = 0;
	const auto text = JS_ToCStringLen(context, &length, args[0]);
	if (!text) {
		return JS_EXCEPTION;
	}
	if (length > MaxFrame || std::string(text, length).find('\n') != std::string::npos) {
		JS_FreeCString(context, text);
		return JS_ThrowRangeError(context, "IPC frame too large or invalid");
	}
	std::cout.write(text, length);
	std::cout << '\n' << std::flush;
	JS_FreeCString(context, text);
	return JS_UNDEFINED;
}

bool Check(JSContext *context, JSValue value) {
	const auto failed = JS_IsException(value);
	JS_FreeValue(context, value);
	if (failed) {
		const auto error = JS_GetException(context);
		const auto text = JS_ToCString(context, error);
		std::cerr << (text ? text : "JavaScript exception") << '\n';
		JS_FreeCString(context, text);
		JS_FreeValue(context, error);
	}
	return !failed;
}

} // namespace

int main() {
	const auto runtime = JS_NewRuntime();
	if (!runtime) {
		return 1;
	}
	JS_SetMemoryLimit(runtime, 64 * 1024 * 1024);
	JS_SetMaxStackSize(runtime, 512 * 1024);
	JS_SetCanBlock(runtime, false);
	JS_SetInterruptHandler(runtime, Interrupted, nullptr);
	const auto context = JS_NewContext(runtime);
	if (!context) {
		JS_FreeRuntime(runtime);
		return 1;
	}
	const auto global = JS_GetGlobalObject(context);
	JS_SetPropertyStr(context, global, "__bgPost", JS_NewCFunction(context, Post, "post", 1));
	Deadline = Clock::now() + std::chrono::milliseconds(1000);
	auto ok = Check(context, JS_Eval(context, kPluginSdk, sizeof(kPluginSdk) - 1,
		"budanovgram-sdk.js", JS_EVAL_TYPE_GLOBAL));
	const auto dispatch = JS_GetPropertyStr(context, global, "__bgDispatch");
	JS_FreeValue(context, global);
	while (ok && std::cin.good()) {
		std::string line;
		char ch = 0;
		while (std::cin.get(ch) && ch != '\n') {
			if (line.size() >= MaxFrame) {
				ok = false;
				break;
			}
			line += ch;
		}
		if (!ok || line.empty()) {
			break;
		}
		Deadline = Clock::now() + std::chrono::milliseconds(500);
		auto arg = JS_NewStringLen(context, line.data(), line.size());
		ok = Check(context, JS_Call(context, dispatch, JS_UNDEFINED, 1, &arg));
		JS_FreeValue(context, arg);
		JSContext *jobContext = nullptr;
		while (ok && JS_IsJobPending(runtime)) {
			if (Clock::now() > Deadline) {
				ok = false;
				break;
			}
			if (JS_ExecutePendingJob(runtime, &jobContext) < 0) {
				ok = Check(jobContext, JS_EXCEPTION);
			}
		}
		std::cout << "{\"type\":\"idle\"}\n" << std::flush;
	}
	JS_FreeValue(context, dispatch);
	JS_FreeContext(context);
	JS_FreeRuntime(runtime);
	return ok ? 0 : 2;
}
