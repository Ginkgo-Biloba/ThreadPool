#pragma once

#include "atomic.hpp"

namespace gk {

struct RefObj {
	uint32_t refcount;

	RefObj() { refcount = 0; }

	// You can override this function
	virtual void destory() { delete this; }

protected:
	virtual ~RefObj() { GK_ASSERT(refcount == 0); }

private:
	RefObj(RefObj const&) = delete;
	RefObj(RefObj&&) = delete;
	RefObj& operator=(RefObj const&) = delete;
	RefObj& operator=(RefObj&&) = delete;
};

#define GK_REFPTR_ENABLE_IF(X, Y) \
	typename std::enable_if<std::is_convertible<X*, Y*>::value, int>::type = 0

template <typename T>
class RefPtr {
	static_assert(std::is_base_of<RefObj, T>::value, "");

	template <typename U>
	friend class RefPtr;

	T* obj;

	void addref()
	{
		if (obj) atomic_fetch_add(&(obj->refcount), 1);
	}

	void destroy()
	{
		if (obj && atomic_fetch_add(&(obj->refcount), -1) == 1)
			obj->destory();
		obj = nullptr;
	}

public:
	void swap(RefPtr& other) noexcept
	{
		T* tmp = obj;
		obj = other.obj;
		other.obj = tmp;
	}

	~RefPtr() { destroy(); }

	RefPtr() { obj = nullptr; }

	RefPtr(std::nullptr_t) { obj = nullptr; }

	RefPtr(T* ptr)
	{
		GK_ASSERT(ptr->refcount == 0);
		obj = ptr;
		addref();
	}

	RefPtr(RefPtr const& other)
	{
		obj = other.obj;
		addref();
	}

	RefPtr(RefPtr&& other)
	{
		obj = other.obj;
		other.obj = nullptr;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(U* ptr)
	{
		obj = static_cast<T*>(ptr);
		addref();
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U> const& other)
	{
		obj = static_cast<T*>(other.obj);
		addref();
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U>&& other)
	{
		obj = static_cast<T*>(other.obj);
		other.obj = nullptr;
	}

	RefPtr& operator=(std::nullptr_t)
	{
		destroy();
		return *this;
	}

	RefPtr& operator=(T* ptr)
	{
		RefPtr(ptr).swap(*this);
		return *this;
	}

	RefPtr& operator=(RefPtr const& other)
	{
		if (obj != other.obj) RefPtr(other).swap(*this);
		return *this;
	}

	RefPtr& operator=(RefPtr&& other)
	{
		RefPtr(static_cast<RefPtr&&>(other)).swap(*this);
		return *this;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(U* ptr)
	{
		RefPtr(ptr).swap(*this);
		return *this;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(RefPtr<U> const& other)
	{
		if (obj != static_cast<T*>(other)) RefPtr(other).swap(*this);
		return *this;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(RefPtr<U>&& other)
	{
		RefPtr(static_cast<RefPtr<U>&&>(other)).swap(*this);
		return *this;
	}

	T* get() const noexcept { return obj; }
	T* operator->() const noexcept { return obj; }
	explicit operator bool() const noexcept { return static_cast<bool>(obj); }

	template <typename U>
	RefPtr<U> staticTo() noexcept
	{
		RefPtr<U> u;
		u.obj = static_cast<U*>(obj);
		addref();
		return u;
	}

	template <typename U>
	RefPtr<U> dynamicTo() noexcept
	{
		RefPtr<U> u;
		u.obj = dynamic_cast<U*>(obj);
		addref();
		return u;
	}
};

#define GK_REFPTR_OP(op)                                            \
	template <typename T, typename U>                                 \
	bool operator op(RefPtr<T> const& a, RefPtr<U> const& b) noexcept \
	{                                                                 \
		return a.get() op b.get();                                      \
	}

GK_REFPTR_OP(==)
GK_REFPTR_OP(!=)
GK_REFPTR_OP(<)
GK_REFPTR_OP(<=)
GK_REFPTR_OP(>)
GK_REFPTR_OP(>=)

#undef GK_REFPTR_OP
#undef GK_REFPTR_ENABLE_IF

}
