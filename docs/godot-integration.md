# Godot 4 Integration Guide

This guide explains how to use the `kalshi-cpp` SDK with Godot 4, covering both GDScript (via GDExtension) and C# (via P/Invoke).

## Overview

Since this is a C++23 library, there are two primary integration paths:

| Approach | Language | Difficulty | Cross-Platform |
| -------- | -------- | ---------- | -------------- |
| GDExtension | GDScript | Medium | ✅ Yes |
| P/Invoke (C ABI wrapper) | C# | Medium | ✅ Yes |

Both approaches require building a native library that bridges between Godot and the SDK.

---

## Prerequisites

### All Platforms

- Godot 4.x (with .NET workload for C#)
- CMake 3.20+
- C++23 compiler (GCC 13+, Clang 16+, MSVC 2022+)
- OpenSSL, libcurl, libwebsockets development libraries

### Platform-Specific

**Linux (Ubuntu/Debian):**

```bash
sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev libwebsockets-dev
```

**macOS:**

```bash
brew install cmake openssl curl libwebsockets
```

**Windows:**

```powershell
# Use vcpkg for dependencies
vcpkg install openssl curl libwebsockets --triplet=x64-windows
```

---

## Option 1: GDExtension (GDScript)

GDExtension lets you expose C++ classes directly to GDScript. You'll create a thin wrapper that exposes `kalshi-cpp` functionality.

### Step 1: Build and Install kalshi-cpp

```bash
cd kalshi-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./install
cmake --build build
cmake --install build
```

This produces:

- `install/lib/` – static libraries (`libkalshi_*.a` / `.lib`)
- `install/include/kalshi/` – headers

### Step 2: Set Up GDExtension Project

Create a directory structure for your extension:

```text
my_kalshi_extension/
├── src/
│   ├── register_types.cpp
│   ├── register_types.h
│   └── kalshi_client.cpp
│   └── kalshi_client.h
├── godot-cpp/              # Godot C++ bindings (submodule)
├── kalshi-cpp/             # This SDK (submodule or copy)
├── SConstruct
└── my_kalshi.gdextension
```

### Step 3: Create the Wrapper Class

**src/kalshi_client.h:**

```cpp
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <kalshi/kalshi.hpp>
#include <memory>

class KalshiClient : public godot::RefCounted {
    GDCLASS(KalshiClient, RefCounted)

private:
    std::unique_ptr<kalshi::Signer> signer_;
    std::unique_ptr<kalshi::HttpClient> http_;
    std::unique_ptr<kalshi::KalshiClient> client_;

protected:
    static void _bind_methods();

public:
    KalshiClient();
    ~KalshiClient();

    bool initialize(const godot::String& key_id, const godot::String& pem_path);
    godot::Dictionary get_balance();
    godot::Array get_markets(int limit);
};
```

**src/kalshi_client.cpp:**

```cpp
#include "kalshi_client.h"
#include <godot_cpp/variant/utility_functions.hpp>

void KalshiClient::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("initialize", "key_id", "pem_path"), 
                                 &KalshiClient::initialize);
    godot::ClassDB::bind_method(godot::D_METHOD("get_balance"), 
                                 &KalshiClient::get_balance);
    godot::ClassDB::bind_method(godot::D_METHOD("get_markets", "limit"), 
                                 &KalshiClient::get_markets);
}

KalshiClient::KalshiClient() = default;
KalshiClient::~KalshiClient() = default;

bool KalshiClient::initialize(const godot::String& key_id, const godot::String& pem_path) {
    auto signer = kalshi::Signer::from_pem_file(
        key_id.utf8().get_data(), 
        pem_path.utf8().get_data()
    );
    if (!signer) {
        godot::UtilityFunctions::printerr("Kalshi init failed: ", signer.error().message.c_str());
        return false;
    }
    signer_ = std::make_unique<kalshi::Signer>(std::move(*signer));
    http_ = std::make_unique<kalshi::HttpClient>(*signer_);
    client_ = std::make_unique<kalshi::KalshiClient>(std::move(*http_));
    return true;
}

godot::Dictionary KalshiClient::get_balance() {
    godot::Dictionary result;
    if (!client_) return result;
    
    auto balance = client_->get_balance();
    if (balance) {
        result["balance"] = balance->balance;
        result["available"] = balance->available_balance;
    }
    return result;
}

godot::Array KalshiClient::get_markets(int limit) {
    godot::Array arr;
    if (!client_) return arr;
    
    kalshi::GetMarketsParams params;
    params.limit = limit;
    auto markets = client_->get_markets(params);
    if (markets) {
        for (const auto& m : markets->items) {
            godot::Dictionary d;
            d["ticker"] = m.ticker.c_str();
            d["title"] = m.title.c_str();
            arr.push_back(d);
        }
    }
    return arr;
}
```

**src/register_types.cpp:**

```cpp
#include "register_types.h"
#include "kalshi_client.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

void initialize_kalshi_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) return;
    godot::ClassDB::register_class<KalshiClient>();
}

void uninitialize_kalshi_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
GDExtensionBool GDE_EXPORT kalshi_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization
) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_kalshi_module);
    init_obj.register_terminator(uninitialize_kalshi_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
```

### Step 4: Build with SCons

**SConstruct** (simplified):

```python
#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/", "kalshi-cpp/install/include"])
env.Append(LIBPATH=["kalshi-cpp/install/lib"])
env.Append(LIBS=["kalshi_api", "kalshi_ws", "kalshi_http", "kalshi_auth", 
                 "kalshi_models", "kalshi_core", "ssl", "crypto", "curl", "websockets"])

sources = Glob("src/*.cpp")
library = env.SharedLibrary(
    "bin/libkalshi_godot{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources
)
Default(library)
```

Build:

```bash
scons platform=linux target=template_release
# or: scons platform=windows target=template_release
# or: scons platform=macos target=template_release
```

### Step 5: Create .gdextension File

**my_kalshi.gdextension:**

```ini
[configuration]
entry_symbol = "kalshi_library_init"
compatibility_minimum = "4.2"

[libraries]
linux.release.x86_64 = "res://bin/libkalshi_godot.linux.template_release.x86_64.so"
windows.release.x86_64 = "res://bin/libkalshi_godot.windows.template_release.x86_64.dll"
macos.release = "res://bin/libkalshi_godot.macos.template_release.framework"
```

### Step 6: Use in GDScript

```gdscript
extends Node

var kalshi: KalshiClient

func _ready():
    kalshi = KalshiClient.new()
    if kalshi.initialize("your-key-id", "res://keys/api_key.pem"):
        var balance = kalshi.get_balance()
        print("Balance: $", balance.get("balance", 0) / 100.0)
        
        var markets = kalshi.get_markets(5)
        for m in markets:
            print(m.ticker, ": ", m.title)
```

---

## Option 2: C# with P/Invoke

For Godot's .NET version, create a C ABI wrapper around the SDK and call it via P/Invoke.

### Step 1: Create C ABI Wrapper

**kalshi_c_api.h:**

```c
#ifndef KALSHI_C_API_H
#define KALSHI_C_API_H

#ifdef _WIN32
    #define KALSHI_EXPORT __declspec(dllexport)
#else
    #define KALSHI_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* KalshiHandle;

KALSHI_EXPORT KalshiHandle kalshi_create(const char* key_id, const char* pem_path);
KALSHI_EXPORT void kalshi_destroy(KalshiHandle handle);
KALSHI_EXPORT int kalshi_get_balance(KalshiHandle handle, long* balance, long* available);
KALSHI_EXPORT int kalshi_get_markets_json(KalshiHandle handle, int limit, char* buffer, int buffer_size);

#ifdef __cplusplus
}
#endif

#endif
```

**kalshi_c_api.cpp:**

```cpp
#include "kalshi_c_api.h"
#include <kalshi/kalshi.hpp>
#include <cstring>
#include <sstream>

struct KalshiContext {
    kalshi::Signer signer;
    kalshi::HttpClient http;
    kalshi::KalshiClient client;
    
    KalshiContext(kalshi::Signer s)
        : signer(std::move(s)), http(signer), client(std::move(http)) {}
};

extern "C" {

KALSHI_EXPORT KalshiHandle kalshi_create(const char* key_id, const char* pem_path) {
    auto signer = kalshi::Signer::from_pem_file(key_id, pem_path);
    if (!signer) return nullptr;
    return new KalshiContext(std::move(*signer));
}

KALSHI_EXPORT void kalshi_destroy(KalshiHandle handle) {
    delete static_cast<KalshiContext*>(handle);
}

KALSHI_EXPORT int kalshi_get_balance(KalshiHandle handle, long* balance, long* available) {
    if (!handle) return -1;
    auto* ctx = static_cast<KalshiContext*>(handle);
    auto result = ctx->client.get_balance();
    if (!result) return -1;
    *balance = result->balance;
    *available = result->available_balance;
    return 0;
}

KALSHI_EXPORT int kalshi_get_markets_json(KalshiHandle handle, int limit, char* buffer, int buffer_size) {
    if (!handle) return -1;
    auto* ctx = static_cast<KalshiContext*>(handle);
    kalshi::GetMarketsParams params;
    params.limit = limit;
    auto result = ctx->client.get_markets(params);
    if (!result) return -1;
    
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& m : result->items) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"ticker\":\"" << m.ticker << "\",\"title\":\"" << m.title << "\"}";
    }
    oss << "]";
    
    std::string json = oss.str();
    if ((int)json.size() >= buffer_size) return -2;
    std::strcpy(buffer, json.c_str());
    return (int)json.size();
}

}
```

### Step 2: Build as Shared Library

**CMakeLists.txt** (for the C wrapper):

```cmake
cmake_minimum_required(VERSION 3.20)
project(kalshi_c_api)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(kalshi CONFIG REQUIRED)

add_library(kalshi_c SHARED kalshi_c_api.cpp)
target_link_libraries(kalshi_c PRIVATE kalshi::kalshi)
set_target_properties(kalshi_c PROPERTIES
    VISIBILITY_INLINES_HIDDEN ON
    CXX_VISIBILITY_PRESET hidden
)
```

Build:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/kalshi-cpp/install
cmake --build build
```

### Step 3: Use in C# (Godot .NET)

**KalshiNative.cs:**

```csharp
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

public static class KalshiNative
{
    private const string LibName = "kalshi_c";

    [DllImport(LibName)] private static extern IntPtr kalshi_create(string keyId, string pemPath);
    [DllImport(LibName)] private static extern void kalshi_destroy(IntPtr handle);
    [DllImport(LibName)] private static extern int kalshi_get_balance(IntPtr handle, out long balance, out long available);
    [DllImport(LibName)] private static extern int kalshi_get_markets_json(IntPtr handle, int limit, StringBuilder buffer, int bufferSize);

    private static IntPtr _handle;

    public static bool Initialize(string keyId, string pemPath)
    {
        _handle = kalshi_create(keyId, pemPath);
        return _handle != IntPtr.Zero;
    }

    public static void Shutdown()
    {
        if (_handle != IntPtr.Zero)
        {
            kalshi_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }

    public static (long balance, long available)? GetBalance()
    {
        if (_handle == IntPtr.Zero) return null;
        if (kalshi_get_balance(_handle, out long bal, out long avail) == 0)
            return (bal, avail);
        return null;
    }

    public static JsonDocument? GetMarkets(int limit = 10)
    {
        if (_handle == IntPtr.Zero) return null;
        var buffer = new StringBuilder(65536);
        int len = kalshi_get_markets_json(_handle, limit, buffer, buffer.Capacity);
        if (len < 0) return null;
        return JsonDocument.Parse(buffer.ToString(0, len));
    }
}
```

**Usage in Godot C# script:**

```csharp
using Godot;

public partial class Main : Node
{
    public override void _Ready()
    {
        if (KalshiNative.Initialize("your-key-id", "res://keys/api_key.pem"))
        {
            var balance = KalshiNative.GetBalance();
            if (balance.HasValue)
                GD.Print($"Balance: ${balance.Value.balance / 100.0}");

            var markets = KalshiNative.GetMarkets(5);
            if (markets != null)
            {
                foreach (var m in markets.RootElement.EnumerateArray())
                    GD.Print($"{m.GetProperty("ticker")}: {m.GetProperty("title")}");
            }
        }
    }

    public override void _ExitTree()
    {
        KalshiNative.Shutdown();
    }
}
```

---

## Platform Notes

### Shared Library Locations

Place the compiled native library where Godot can find it:

| Platform | Library Name | Location |
| -------- | ------------ | -------- |
| Linux | `libkalshi_c.so` | Project root or `/usr/lib` |
| macOS | `libkalshi_c.dylib` | Project root or `/usr/local/lib` |
| Windows | `kalshi_c.dll` | Project root or `PATH` |

### Export Templates

When exporting your Godot project, ensure native libraries are included:

1. **GDExtension**: Libraries listed in `.gdextension` are bundled automatically.
2. **C#/P/Invoke**: Add the `.dll`/`.so`/`.dylib` to export filters in Project Settings → Export.

---

## Limitations & Gotchas

1. **C++23 Requirement**: Ensure your compiler supports C++23. On Windows, use Visual Studio 2022 17.5+.

2. **Static vs Shared Linking**: The SDK builds as static libraries. Link them into your GDExtension or C wrapper at build time.

3. **OpenSSL/libcurl/libwebsockets**: These dependencies must be available at runtime. Consider static linking for easier distribution.

4. **Thread Safety**: API calls should be made from a single thread or protected with mutexes. Godot's `_process` runs on the main thread.

5. **Blocking Calls**: REST API calls block. For responsive UIs, run them in a `Thread` or use Godot's `WorkerThreadPool`.

6. **WebSocket Integration**: For real-time streaming, the WebSocket client needs its own event loop. Consider exposing a simplified polling API or integrating with Godot's networking.

7. **Error Handling**: The examples above use minimal error handling. Production code should check all return values and handle failures gracefully.

---

## References

- [Godot GDExtension Docs](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/what_is_gdextension.html)
- [godot-cpp Repository](https://github.com/godotengine/godot-cpp)
- [Godot C# Docs](https://docs.godotengine.org/en/stable/tutorials/scripting/c_sharp/index.html)
- [kalshi-cpp README](../README.md)
