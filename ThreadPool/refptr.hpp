#pragma once

#include "atomic.hpp"

namespace gk {

struct RefObj {
	int refcount;

	RefObj() { refcount = 0; }
	virtual ~RefObj() { GK_ASSERT(refcount == 0); }

	int addref(int x) { return atomic_fetch_add(&refcount, x); }

	RefObj(RefObj const&) = delete;
	RefObj(RefObj&&) = delete;
	RefObj& operator=(RefObj const&) = delete;
	RefObj& operator=(RefObj&&) = delete;
};

#define GK_REFPTR_ENABLE_IF(X, Y) \
	typename std::enable_if<std::is_convertible<X*, Y*>::value, int>::type = 0

template <typename T>
class RefPtr final {
	static_assert(std::is_base_of<RefObj, T>::value, "");

	T* obj;

	void destroy()
	{
		if (obj && obj->addref(-1) == 1)
			delete obj;
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
		obj = ptr;
		if (obj) obj->addref(1);
	}

	RefPtr(RefPtr const& other)
	{
		obj = other.obj;
		if (obj) obj->addref(1);
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
		if (obj) obj->addref(1);
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U> const& other)
	{
		obj = static_cast<T*>(other.obj);
		if (obj) obj->addref(1);
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U>&& other)
	{
		obj = static_cast<T*>(other.obj);
		other.obj = nullptr;
	}

	RefPtr& operator=(std::nullptr_t) { destroy(); }

	RefPtr& operator=(T* ptr) { RefPtr(ptr).swap(*this); }

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
	RefPtr& operator=(U* ptr) { RefPtr(ptr).swap(*this); }

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
	explicit operator bool() const noexcept { return !!obj; }

	template <typename U>
	RefPtr<U> staticTo() noexcept
	{
		RefPtr<U> u;
		u.obj = static_cast<U*>(obj);
		if (obj) obj->addref(1);
		return u;
	}

	template <typename U>
	RefPtr<U> dynamicTo() noexcept
	{
		RefPtr<U> u;
		u.obj = dynamic_cast<U*>(obj);
		if (obj) obj->addref(1);
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
