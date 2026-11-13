#pragma once
struct android_app;
namespace ember::platform::detail {
android_app *android_context();
void set_android_context(android_app *);
}
