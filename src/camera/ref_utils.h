#pragma once

#include <reframework/API.hpp>
#include <vector>

namespace RE2HT {

// Shared empty args vector for REFramework method invocations
inline const std::vector<void*>& EmptyArgs() {
    static const std::vector<void*> args;
    return args;
}

// Invoke a REFramework method with no arguments, returning the pointer result
inline void* InvokePtr(reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<reframework::API::ManagedObject*>(obj), EmptyArgs());
    return ret.ptr;
}

// Invoke a no-argument method returning a managed bool (byte-sized result)
inline bool InvokeBool(reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<reframework::API::ManagedObject*>(obj), EmptyArgs());
    return ret.byte != 0;
}

} // namespace RE2HT
