#pragma once

#include <iostream>
#include <memory>

template<typename T>
class EnableSharedFromThis;

template<typename T>
class WeakPtr;

template<typename T>
class SharedPtr;

template<typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args);

template<typename T>
class SharedPtr {
private:
    template<typename S, typename Alloc, typename... Args>
    friend SharedPtr<S> allocateShared(const Alloc& alloc, Args&&... args);

    template<typename Y>
    friend class WeakPtr;

    template<typename Y>
    friend class SharedPtr;

    struct BaseControlBlock {
        size_t shared_count = 0;
        size_t weak_count = 0;
        virtual T* get_pointer() = 0;
        virtual void destroy_object() noexcept = 0;
        virtual ~BaseControlBlock() = default;
    };

    template<typename Y, typename Alloc>
    struct ControlBlockEmbeded final : public BaseControlBlock {
        [[no_unique_address]] Alloc alloc;
        std::aligned_storage_t<sizeof(Y), alignof(Y)> obj;
        T* get_pointer() final;
        void destroy_object() noexcept final;
        template<typename... Args>
        ControlBlockEmbeded(Alloc alloc, Args&&... args)
                : BaseControlBlock()
                , alloc(alloc) {
            new (&obj) Y(std::forward<Args>(args)...);
        }
        ~ControlBlockEmbeded() final;
    };

    template<typename Y, typename Deleter, typename Alloc>
    struct ControlBlockFromPointer final : public BaseControlBlock {
        [[no_unique_address]] Deleter del;
        [[no_unique_address]] Alloc alloc;
        Y* pobj;
        T* get_pointer() final;
        void destroy_object() noexcept final;
        template<typename... Args>
        ControlBlockFromPointer(Deleter del, Alloc alloc, Y* pointer)
                : BaseControlBlock()
                , del(del)
                , alloc(alloc)
                , pobj(pointer)
                {}
        ~ControlBlockFromPointer() final;
    };

    BaseControlBlock* control_block = nullptr;
    T* pointer;

    void unlink_shared_pointer_from_the_control_block() noexcept;
    SharedPtr(T* pointer, BaseControlBlock* control_block)
            : control_block(control_block)
            , pointer(pointer) {
        ++control_block->shared_count;
        if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
            pointer->weak_this = *this;
        }
    }
public:
    SharedPtr(): control_block(nullptr), pointer(nullptr) {}
    template<typename Y, typename Deleter, typename Alloc>
    SharedPtr(Y* given_pointer, Deleter del, Alloc alloc)
            : pointer(static_cast<T*>(given_pointer)) {
        typename std::allocator_traits<Alloc>::
                 template rebind_alloc<ControlBlockFromPointer<Y, Deleter, Alloc>> pointer_control_block_alloc = alloc;
        using AllocTraits = std::allocator_traits<decltype(pointer_control_block_alloc)>;
        auto pointer_control_block = AllocTraits::allocate(pointer_control_block_alloc, 1);
        control_block = pointer_control_block;
        new (pointer_control_block) ControlBlockFromPointer<Y, Deleter, Alloc>(del, alloc, pointer);
        if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
            pointer->weak_this = control_block;
        }
        ++control_block->shared_count;
    }

    template<typename Y, typename Deleter>
    SharedPtr(Y* pointer, Deleter del)
            : SharedPtr(pointer, del, std::allocator<Y>())
            {}

    template<typename Y>
    SharedPtr(Y* pointer)
            : SharedPtr(pointer, std::default_delete<Y>(), std::allocator<Y>())
            {}

    template<typename Y>
    SharedPtr(const SharedPtr<Y>& other)
            : control_block(reinterpret_cast<decltype(control_block)>(other.control_block))
            , pointer(static_cast<T*>(other.pointer)) {
        if (control_block) ++control_block->shared_count;
    }

    SharedPtr(const SharedPtr& other)
            : control_block(other.control_block)
            , pointer(other.pointer) {
        if (control_block) ++control_block->shared_count;
    }

    template<typename Y>
    SharedPtr(SharedPtr<Y>&& other)
            : control_block(reinterpret_cast<decltype(control_block)>(other.control_block))
            , pointer(static_cast<T*>(other.pointer)) {
        other.pointer = nullptr;
        other.control_block = nullptr;
    }

    SharedPtr(SharedPtr&& other)
            : control_block(other.control_block)
            , pointer(other.pointer) {
        other.pointer = nullptr;
        other.control_block = nullptr;
    }

    template<typename Y>
    SharedPtr& operator=(const SharedPtr<Y>& other) {
        SharedPtr copy = other;
        swap(copy);
        return *this;
    }

    SharedPtr& operator=(const SharedPtr& other) {
        if (&other == this) return *this;
        return operator=<T>(other);
    }

    template<typename Y>
    SharedPtr& operator=(SharedPtr<Y>&& other) {
        SharedPtr moved_other = std::move(other);
        swap(moved_other);
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) {
        if (&other == this) return *this;
        return operator=<T>(std::move(other));
    }
    
    ~SharedPtr() {
        unlink_shared_pointer_from_the_control_block();
    }

    size_t use_count() const {
        return control_block->shared_count;
    }

    void reset() {
        unlink_shared_pointer_from_the_control_block();
    }
    
    template<typename Y>
    void reset(Y* new_pointer) {
        operator=(SharedPtr(new_pointer));
    }

    template<typename Y>
    void swap(SharedPtr<Y>& other);

    T& operator*() {
        return *(control_block->get_pointer());
    }

    const T& operator*() const {
        return *(control_block->get_pointer());
    }

    T* operator->() {
        return control_block->get_pointer();
    }

    const T* operator->() const {
        return control_block->get_pointer();
    }

    T* get() {
        return pointer;
    }

    const T* get() const {
        return pointer;
    }
};

template<typename T>
template<typename Y>
void SharedPtr<T>::swap(SharedPtr<Y>& other) {
    std::swap(control_block, other.control_block);
    std::swap(pointer, other.pointer);
}

template<typename T>
class EnableSharedFromThis {
private:
    WeakPtr<T> weak_this;

    template<typename Y>
    friend class SharedPtr;
public:
    SharedPtr<T> shared_from_this();
};

template<typename T>
SharedPtr<T> EnableSharedFromThis<T>::shared_from_this() {
    if (weak_this.expired()) throw std::bad_weak_ptr();
    return weak_this.lock();
}

template<typename T>
void SharedPtr<T>::unlink_shared_pointer_from_the_control_block() noexcept {
    if (control_block == nullptr) return;
    if (control_block->shared_count > 1) {
        --control_block->shared_count;
        pointer = nullptr;
        control_block = nullptr;
        return;
    }
    control_block->destroy_object();
    --control_block->shared_count;
    if (control_block->weak_count == 0) {
        control_block->~BaseControlBlock();
    }
    pointer = nullptr;
    control_block = nullptr;
}

template<typename T>
template<typename Y, typename Alloc>
void SharedPtr<T>::ControlBlockEmbeded<Y, Alloc>::destroy_object() noexcept {
    typename std::allocator_traits<Alloc>::
             template rebind_alloc<Y> obj_alloc = alloc;
    using AllocTraits = std::allocator_traits<decltype(obj_alloc)>;
    AllocTraits::destroy(obj_alloc, reinterpret_cast<Y*>(&obj));
}

template<typename T>
template<typename Y, typename Alloc>
T* SharedPtr<T>::ControlBlockEmbeded<Y, Alloc>::get_pointer() {
    return reinterpret_cast<T*>(&obj);
}

template<typename T>
template<typename Y, typename Deleter, typename Alloc>
void SharedPtr<T>::ControlBlockFromPointer<Y, Deleter, Alloc>::destroy_object() noexcept {
    del(pobj);
}

template<typename T>
template<typename Y, typename Deleter, typename Alloc>
T* SharedPtr<T>::ControlBlockFromPointer<Y, Deleter, Alloc>::get_pointer() {
    return pobj;
}

template<typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(const Alloc& alloc, Args&&... args) {
    typename std::allocator_traits<Alloc>::
             template rebind_alloc<typename SharedPtr<T>::
             template ControlBlockEmbeded<T, Alloc>> embedded_control_block_alloc = alloc;
    using AllocTraits = std::allocator_traits<decltype(embedded_control_block_alloc)>;
    auto embedded_control_block = AllocTraits::allocate(embedded_control_block_alloc, 1);
    AllocTraits::construct(embedded_control_block_alloc
                          , embedded_control_block
                          , alloc
                          , std::forward<Args>(args)...);
    return SharedPtr<T>(reinterpret_cast<T*>(&embedded_control_block->obj)
                        , static_cast<typename SharedPtr<T>
                        :: BaseControlBlock*>(embedded_control_block));
}

template<typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return allocateShared<T, std::allocator<T>, Args...>(std::allocator<T>(), std::forward<Args>(args)...);
}

template<typename T>
template<typename Y, typename Alloc>
SharedPtr<T>::ControlBlockEmbeded<Y, Alloc>::~ControlBlockEmbeded() {
    typename std::allocator_traits<Alloc>::
             template rebind_alloc<ControlBlockEmbeded> embeded_control_block_alloc = alloc;
    using AllocTraits = std::allocator_traits<decltype(embeded_control_block_alloc)>;
    AllocTraits::deallocate(embeded_control_block_alloc, this, 1);
}

template<typename T>
template<typename Y, typename Deleter, typename Alloc>
SharedPtr<T>::ControlBlockFromPointer<Y, Deleter, Alloc>::~ControlBlockFromPointer() {
    typename std::allocator_traits<Alloc>::
             template rebind_alloc<ControlBlockFromPointer> pointer_control_block_alloc = alloc;
    using AllocTraits = std::allocator_traits<decltype(pointer_control_block_alloc)>;
    AllocTraits::deallocate(pointer_control_block_alloc, this, 1);
}

template<typename T>
class WeakPtr {
private:
    template<typename U>
    friend class WeakPtr;

    typename SharedPtr<T>::BaseControlBlock* control_block = nullptr;
    void unlink_weak_pointer_from_the_control_block() noexcept;
public:
    WeakPtr();
    template<typename U>
    WeakPtr(SharedPtr<U> pointer);
    WeakPtr(SharedPtr<T> pointer);
    template<typename U>
    WeakPtr(const WeakPtr<U>& other);
    WeakPtr(const WeakPtr& other);
    template<typename U>
    WeakPtr(WeakPtr<U>&& other);
    WeakPtr(WeakPtr&& other);
    template<typename U>
    WeakPtr& operator=(const WeakPtr<U>& other);
    WeakPtr& operator=(const WeakPtr& other);
    template<typename U>
    WeakPtr& operator=(const SharedPtr<U>& other);
    WeakPtr& operator=(const SharedPtr<T>& other);
    template<typename U>
    WeakPtr& operator=(WeakPtr<U>&& other);
    WeakPtr& operator=(WeakPtr&& other);
    ~WeakPtr();
    bool expired() const noexcept;
    SharedPtr<T> lock();
    const SharedPtr<T> lock() const;
    template<typename Y>
    void swap(WeakPtr<Y>& other);
    T& operator*();
    T* operator->();
    void reset();
    size_t use_count() const;
};

template<typename T>
template<typename U>
WeakPtr<T>::WeakPtr(SharedPtr<U> pointer)
        : control_block(reinterpret_cast<decltype(control_block)>(pointer.control_block)) {
    ++control_block->weak_count;
}

template<typename T>
WeakPtr<T>::WeakPtr(SharedPtr<T> pointer)
        : control_block(pointer.control_block) {
    ++control_block->weak_count;
}

template<typename T>
template<typename U>
WeakPtr<T>::WeakPtr(const WeakPtr<U>& other)
        : control_block(reinterpret_cast<decltype(control_block)>(other.control_block)) {
    ++control_block->weak_count;
}

template<typename T>
WeakPtr<T>::WeakPtr(const WeakPtr& other)
        : control_block(other.control_block) {
    ++control_block->weak_count;
}

template<typename T>
template<typename U>
WeakPtr<T>::WeakPtr(WeakPtr<U>&& other)
        : control_block(reinterpret_cast<decltype(control_block)>(other.control_block)) {
    other.control_block = nullptr;
}

template<typename T>
WeakPtr<T>::WeakPtr(WeakPtr&& other)
        : control_block(other.control_block) {
    other.control_block = nullptr;
}

template<typename T>
template<typename U>
WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr<U>& other) {
    WeakPtr<T> copy = other;
    swap(copy);
    return *this;
}

template<typename T>
template<typename U>
WeakPtr<T>& WeakPtr<T>::operator=(const SharedPtr<U>& other) {
    WeakPtr<T> copy = other;
    swap(copy);
    return *this;
}

template<typename T>
WeakPtr<T>& WeakPtr<T>::operator=(const SharedPtr<T>& other) {
    return operator=<T>(other);
}

template<typename T>
WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr& other) {
    if (&other == this) return *this;
    return operator=<T>(other);
}

template<typename T>
template<typename U>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr<U>&& other) {
    WeakPtr<T> moved_copy = std::move(other);
    swap(moved_copy);
    return *this;
}

template<typename T>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr&& other) {
    if (&other == this) return *this;
    return operator=<T>(std::move(other));
}

template<typename T>
WeakPtr<T>::~WeakPtr() {
    unlink_weak_pointer_from_the_control_block();
}

template<typename T>
bool WeakPtr<T>::expired() const noexcept {
    return (control_block == nullptr || control_block->shared_count == 0);
}

template<typename T>
SharedPtr<T> WeakPtr<T>::lock() {
    return (expired() ? SharedPtr<T>()
            : SharedPtr(static_cast<T*>(control_block->get_pointer()), control_block));
}

template<typename T>
const SharedPtr<T> WeakPtr<T>::lock() const {
    return (expired() ? SharedPtr<T>()
            : SharedPtr(static_cast<T*>(control_block->get_pointer()), control_block));
}

template<typename T>
void WeakPtr<T>::unlink_weak_pointer_from_the_control_block() noexcept {
    if (control_block == nullptr) return;
    --control_block->weak_count;
    if (control_block->shared_count == 0 && control_block->weak_count == 0) {
        control_block->~BaseControlBlock();
    }
    control_block = nullptr;
}

template<typename T>
template<typename Y>
void WeakPtr<T>::swap(WeakPtr<Y>& other) {
    std::swap(control_block, other.control_block);
}

template<typename T>
T& WeakPtr<T>::operator*() {
    return *(control_block->get_pointer());
}

template<typename T>
T* WeakPtr<T>::operator->() {
    return control_block->get_pointer();
}

template<typename T>
void WeakPtr<T>::reset() {
    unlink_weak_pointer_from_the_control_block();
}

template<typename T>
WeakPtr<T>::WeakPtr(): control_block(nullptr) {}

template<typename T>
size_t WeakPtr<T>::use_count() const {
    return control_block->shared_count;
}
