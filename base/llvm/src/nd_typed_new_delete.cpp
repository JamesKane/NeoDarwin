// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: C++ operator new and delete overloads; only C++ gives them their Itanium-mangled names and exception behaviour.
//
// Apple's typed operator new and delete: overloads taking a
// std::__type_descriptor_t, the type identity Apple's clang passes when typed
// allocation is on (libc++'s __new/global_typed_new_delete.h in Apple's
// SDK). Apple adds them to its libc++abi, which LLVM's lacks. Allocation goes
// through libmalloc's malloc_type_* calls with the descriptor as the type
// ID, so memory keeps its type segregation as on macOS; failure calls the
// new_handler or throws bad_alloc, as libcxxabi's untyped operators do
// (src/stdlib_new_delete.cpp). Deletion is free(), whatever the variant.
//
// dyld needs them too: when a main executable has weak definitions it binds
// every operator new and delete it may override, the typed ones included
// (dyld-1323.3 common/MachOFile.cpp, sTreatAsWeak), and fails the launch if
// one is exported by no loaded image. They are weak definitions, as the
// untyped ones are, so a program can replace them.

#include <cstddef>
#include <cstdlib>
#include <malloc/_malloc_type.h>
#include <new>

namespace std {
enum class __type_descriptor_t : unsigned long long;
}

using std::__type_descriptor_t;

namespace {

void *
typed_new(std::size_t size, __type_descriptor_t desc)
{
	if (size == 0) {
		size = 1;
	}
	void *p;
	while ((p = malloc_type_malloc(size, static_cast<malloc_type_id_t>(desc))) == nullptr) {
		std::new_handler nh = std::get_new_handler();
		if (nh == nullptr) {
			throw std::bad_alloc();
		}
		nh();
	}
	return p;
}

void *
typed_new_aligned(std::size_t size, std::align_val_t alignment, __type_descriptor_t desc)
{
	if (size == 0) {
		size = 1;
	}
	std::size_t align = static_cast<std::size_t>(alignment);
	if (align < sizeof(void *)) {
		align = sizeof(void *);
	}
	void *p;
	while (malloc_type_posix_memalign(&p, align, size, static_cast<malloc_type_id_t>(desc)) != 0) {
		std::new_handler nh = std::get_new_handler();
		if (nh == nullptr) {
			throw std::bad_alloc();
		}
		nh();
	}
	return p;
}

template <typename F>
void *
no_throw(F allocate) noexcept
{
	try {
		return allocate();
	} catch (...) {
		return nullptr;
	}
}

} // namespace

#define ND_WEAK __attribute__((weak, visibility("default")))

ND_WEAK void *operator new(std::size_t size, __type_descriptor_t desc) { return typed_new(size, desc); }
ND_WEAK void *operator new[](std::size_t size, __type_descriptor_t desc) { return typed_new(size, desc); }
ND_WEAK void *
operator new(std::size_t size, __type_descriptor_t desc, const std::nothrow_t &) noexcept
{
	return no_throw([&] { return typed_new(size, desc); });
}
ND_WEAK void *
operator new[](std::size_t size, __type_descriptor_t desc, const std::nothrow_t &) noexcept
{
	return no_throw([&] { return typed_new(size, desc); });
}
ND_WEAK void *
operator new(std::size_t size, __type_descriptor_t desc, std::align_val_t alignment)
{
	return typed_new_aligned(size, alignment, desc);
}
ND_WEAK void *
operator new[](std::size_t size, __type_descriptor_t desc, std::align_val_t alignment)
{
	return typed_new_aligned(size, alignment, desc);
}
ND_WEAK void *
operator new(std::size_t size, __type_descriptor_t desc, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
	return no_throw([&] { return typed_new_aligned(size, alignment, desc); });
}
ND_WEAK void *
operator new[](std::size_t size, __type_descriptor_t desc, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
	return no_throw([&] { return typed_new_aligned(size, alignment, desc); });
}

ND_WEAK void operator delete(void *p, __type_descriptor_t) noexcept { std::free(p); }
ND_WEAK void operator delete[](void *p, __type_descriptor_t) noexcept { std::free(p); }
ND_WEAK void operator delete(void *p, __type_descriptor_t, std::size_t) noexcept { std::free(p); }
ND_WEAK void operator delete[](void *p, __type_descriptor_t, std::size_t) noexcept { std::free(p); }
ND_WEAK void operator delete(void *p, __type_descriptor_t, const std::nothrow_t &) noexcept { std::free(p); }
ND_WEAK void operator delete[](void *p, __type_descriptor_t, const std::nothrow_t &) noexcept { std::free(p); }
ND_WEAK void operator delete(void *p, __type_descriptor_t, std::align_val_t) noexcept { std::free(p); }
ND_WEAK void operator delete[](void *p, __type_descriptor_t, std::align_val_t) noexcept { std::free(p); }
ND_WEAK void operator delete(void *p, __type_descriptor_t, std::size_t, std::align_val_t) noexcept { std::free(p); }
ND_WEAK void operator delete[](void *p, __type_descriptor_t, std::size_t, std::align_val_t) noexcept { std::free(p); }
ND_WEAK void
operator delete(void *p, __type_descriptor_t, std::align_val_t, const std::nothrow_t &) noexcept
{
	std::free(p);
}
ND_WEAK void
operator delete[](void *p, __type_descriptor_t, std::align_val_t, const std::nothrow_t &) noexcept
{
	std::free(p);
}
