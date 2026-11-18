---
name: cpp
description: Guidelines for modern C++ development with C++20/23/26 standards, memory safety, and performance optimization
metadata:
  maintainer: Mindrally
  source: https://github.com/Mindrally/skills
---

# C++ Development Guidelines

You are an expert in modern C++ development with deep knowledge of C++20/23/26 standards, memory management, and high-performance programming.

## Code Style and Structure

- Write clean, modern C++ code following C++20/23/26 standards
- Use meaningful variable and function names
- Follow the Single Responsibility Principle
- Prefer composition over inheritance
- Keep functions small and focused

## Naming Conventions

- Use PascalCase for classes and structs
- Use camelCase for functions, variables, and methods
- Use SCREAMING_SNAKE_CASE for constants and macros
- Use snake_case for namespaces

## Memory Management

### RAII (Resource Acquisition Is Initialization)
- Use RAII for all resource management
- Wrap resources in classes with proper destructors
- Ensure exception safety through RAII
- Use scope guards for cleanup operations

### Best Practices
- Prefer stack allocation over heap allocation
- Use containers instead of raw arrays

## Modern C++ Features

### Features
- Use structured bindings for tuple unpacking
- Use `std::optional` for values that may not exist
- Use `std::variant` for type-safe unions
- Use `if constexpr` for compile-time conditionals
- Use `std::string_view` for non-owning string references
- Use concepts for template constraints
- Use ranges for cleaner algorithms
- Use `std::span` for non-owning array views
- Use coroutines for asynchronous operations
- Use modules for faster compilation (when supported)

## Error Handling

- Use exceptions for error handling
- Define custom exception types for domain-specific errors
- Use `noexcept` for functions that don't throw
- Catch exceptions by const reference
- Provide strong exception guarantees where possible

## Performance

- Use `const` and `constexpr` liberally
- Prefer move semantics with `std::move`
- Use perfect forwarding with `std::forward`
- Avoid unnecessary copies
- Profile before optimizing
- Use `inline` for small frequently-called functions

## Security

### Buffer Safety
- Use `std::array` instead of C-style arrays
- Use `std::vector` with bounds checking
- Prefer `std::string` over C-style strings
- Use `std::span` for array views

### Type Safety
- Avoid C-style casts; use `static_cast`, `dynamic_cast`, etc.
- Use `enum class` instead of plain enums
- Use `nullptr` instead of `NULL`
- Enable compiler warnings and treat them as errors

## Concurrency

- Use `std::thread` and `std::jthread` for threading
- Use `std::mutex` and `std::lock_guard` for synchronization
- Use `std::atomic` for lock-free operations
- Prefer `std::async` for simple async operations
- Use condition variables for thread coordination


## Documentation

- Use Doxygen-style comments for documentation
- Document public APIs thoroughly
- Include usage examples in documentation
- Keep documentation up to date with code changes
- Document thread safety requirements
