#pragma once
#include <utility>

namespace gfx {

// Owning COM handle. Never calls AddRef: taking a raw pointer adopts the reference
// the caller already holds, which is what every D3D/DXGI/DWrite create call returns.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : ptr_(p) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    ~ComPtr() { reset(); }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    // Out-param slot for CreateFoo(&p) / IID_PPV_ARGS; drops anything already held.
    T** put() {
        reset();
        return &ptr_;
    }

    void** putVoid() {
        reset();
        return reinterpret_cast<void**>(put());
    }

    void reset() {
        if (T* p = std::exchange(ptr_, nullptr)) p->Release();
    }

private:
    T* ptr_ = nullptr;
};

} // namespace gfx
