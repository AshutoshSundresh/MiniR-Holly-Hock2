#pragma once

// Lightweight container types for the MiniR runtime/UI.
// MiniVector is a small dynamic array replacement for std::vector.
// MiniString is a tiny owning string type for the runtime.

#include <stddef.h>
#include <initializer_list>
#include <utility>
#include <new>
#include <cstring>
#include <cstdio>

// ---- MiniVector ----------------------------------------------------------

template<typename T>
class MiniVector {
public:
    MiniVector() : _data(nullptr), _size(0), _capacity(0) {}

    explicit MiniVector(size_t n)
        : _data(nullptr), _size(0), _capacity(0) {
        reserve(n);
        for (size_t i = 0; i < n; ++i) {
            new(&_data[i]) T();
        }
        _size = n;
    }

    MiniVector(std::initializer_list<T> init)
        : MiniVector(init.size()) {
        size_t i = 0;
        for (const auto& v : init) {
            _data[i++] = v;
        }
    }

    MiniVector(const MiniVector& other)
        : MiniVector(other._size) {
        for (size_t i = 0; i < _size; ++i) {
            _data[i] = other._data[i];
        }
    }

    MiniVector(MiniVector&& other) noexcept
        : _data(other._data), _size(other._size), _capacity(other._capacity) {
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
    }

    MiniVector& operator=(const MiniVector& other) {
        if (this == &other) return *this;
        clear();
        reserve(other._size);
        for (std::size_t i = 0; i < other._size; ++i) {
            new(&_data[i]) T(other._data[i]);
        }
        _size = other._size;
        return *this;
    }

    MiniVector& operator=(MiniVector&& other) noexcept {
        if (this == &other) return *this;
        destroy();
        _data = other._data;
        _size = other._size;
        _capacity = other._capacity;
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
        return *this;
    }

    ~MiniVector() {
        destroy();
    }

    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }

    T& operator[](size_t i) { return _data[i]; }
    const T& operator[](size_t i) const { return _data[i]; }

    T& back() { return _data[_size - 1]; }
    const T& back() const { return _data[_size - 1]; }

    T* data() { return _data; }
    const T* data() const { return _data; }

    T* begin() { return _data; }
    T* end() { return _data + _size; }
    const T* begin() const { return _data; }
    const T* end() const { return _data + _size; }

    void clear() {
        for (size_t i = 0; i < _size; ++i) {
            _data[i].~T();
        }
        _size = 0;
    }

    void resize(size_t n) {
        if (n < _size) {
            // Destroy extra elements
            for (size_t i = n; i < _size; ++i) {
                _data[i].~T();
            }
            _size = n;
            return;
        }
        if (n > _size) {
            reserve(n);
            for (size_t i = _size; i < n; ++i) {
                new(&_data[i]) T();
            }
            _size = n;
        }
    }

    void resize(size_t n, const T& value) {
        if (n < _size) {
            for (size_t i = n; i < _size; ++i) {
                _data[i].~T();
            }
            _size = n;
            return;
        }
        if (n > _size) {
            reserve(n);
            for (size_t i = _size; i < n; ++i) {
                new(&_data[i]) T(value);
            }
            _size = n;
        }
    }

    void push_back(const T& v) {
        if (_size == _capacity) {
            reserve(_capacity == 0 ? 16 : _capacity * 2);
        }
        new(&_data[_size]) T(v);
        ++_size;
    }

    void push_back(T&& v) {
        if (_size == _capacity) {
            reserve(_capacity == 0 ? 16 : _capacity * 2);
        }
        new(&_data[_size]) T(std::move(v));
        ++_size;
    }

    void pop_back() {
        if (_size == 0) return;
        --_size;
        _data[_size].~T();
    }

    // Very small erase implementation used only with begin() in this project.
    T* erase(T* it) {
        if (it < _data || it >= _data + _size) return it;
        size_t idx = static_cast<size_t>(it - _data);
        _data[idx].~T();
        for (size_t i = idx; i + 1 < _size; ++i) {
            new(&_data[i]) T(std::move(_data[i + 1]));
            _data[i + 1].~T();
        }
        --_size;
        return _data + idx;
    }

    // Simple erase range [first, last) used with algorithms like std::unique/remove_if.
    T* erase(T* first, T* last) {
        if (first == last) return first;
        size_t start_idx = static_cast<size_t>(first - _data);
        size_t count = static_cast<size_t>(last - first);
        for (size_t i = 0; i < count; ++i)
            erase(_data + start_idx);  // always erase same position
        return _data + start_idx;
    }

    template<typename InputIt>
    T* insert(T* pos, InputIt first, InputIt last) {
        if (pos < _data || pos > _data + _size) return pos;
        size_t idx = static_cast<size_t>(pos - _data);
        size_t count = 0;
        for (InputIt it = first; it != last; ++it) ++count;
        if (count == 0) return _data + idx;

        reserve(_size + count);

        // Move tail elements up to make room
        for (size_t i = _size; i > idx; --i) {
            new(&_data[i + count - 1]) T(std::move(_data[i - 1]));
            _data[i - 1].~T();
        }

        // Copy new elements into the gap
        size_t insert_pos = idx;
        for (InputIt it = first; it != last; ++it) {
            new(&_data[insert_pos++]) T(*it);
        }

        _size += count;
        return _data + idx;
    }

    void reserve(size_t new_cap) {
        if (new_cap <= _capacity) return;
        T* new_data = static_cast<T*>(operator new[](new_cap * sizeof(T)));
        for (size_t i = 0; i < _size; ++i) {
            new(&new_data[i]) T(std::move(_data[i]));
            _data[i].~T();
        }
        operator delete[](_data);
        _data = new_data;
        _capacity = new_cap;
    }

private:
    void destroy() {
        clear();
        operator delete[](_data);
        _data = nullptr;
        _capacity = 0;
    }

    T* _data;
    size_t _size;
    size_t _capacity;
};

// ---- MiniString ----------------------------------------------------------

class MiniString {
public:
    MiniString() : _data(nullptr), _size(0), _capacity(0) {}

    MiniString(const char* s) : _data(nullptr), _size(0), _capacity(0) {
        if (s) {
            size_t len = std::strlen(s);
            reserve(len + 1);
            std::memcpy(_data, s, len + 1);
            _size = len;
        }
    }

    MiniString(const MiniString& other) : _data(nullptr), _size(0), _capacity(0) {
        if (!other.empty()) {
            reserve(other._size + 1);
            std::memcpy(_data, other._data, other._size + 1);
            _size = other._size;
        }
    }

    MiniString(MiniString&& other) noexcept
        : _data(other._data), _size(other._size), _capacity(other._capacity) {
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
    }

    MiniString& operator=(const MiniString& other) {
        if (this == &other) return *this;
        clear();
        if (!other.empty()) {
            reserve(other._size + 1);
            std::memcpy(_data, other._data, other._size + 1);
            _size = other._size;
        }
        return *this;
    }

    MiniString& operator=(MiniString&& other) noexcept {
        if (this == &other) return *this;
        if (_data) {
            delete[] _data;
        }
        _data = other._data;
        _size = other._size;
        _capacity = other._capacity;
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
        return *this;
    }

    MiniString& operator=(const char* s) {
        clear();
        if (s) {
            size_t len = std::strlen(s);
            reserve(len + 1);
            std::memcpy(_data, s, len + 1);
            _size = len;
        }
        return *this;
    }

    ~MiniString() {
        if (_data) delete[] _data;
    }

    size_t size() const { return _size; }
    size_t length() const { return _size; }
    bool empty() const { return _size == 0; }

    const char* c_str() const {
        if (!_data) return "";
        _data[_size] = '\0';
        return _data;
    }

    const char* data() const { return c_str(); }
    char* data() {
        if (!_data) {
            reserve(1);
            _data[0] = '\0';
        }
        _data[_size] = '\0';
        return _data;
    }

    char& operator[](size_t i) { return data()[i]; }
    const char& operator[](size_t i) const { return c_str()[i]; }

    void clear() {
        _size = 0;
        if (_data) _data[0] = '\0';
    }

    void reserve(size_t new_cap) {
        if (new_cap <= _capacity) return;
        char* new_data = new char[new_cap];
        if (_data && _capacity) {
            std::memcpy(new_data, _data, _size + 1);
            delete[] _data;
        } else {
            new_data[0] = '\0';
        }
        _data = new_data;
        _capacity = new_cap;
    }

    MiniString& operator+=(const MiniString& other) {
        if (other._size == 0) return *this;
        ensure_capacity(_size + other._size + 1);
        std::memcpy(_data + _size, other.c_str(), other._size);
        _size += other._size;
        _data[_size] = '\0';
        return *this;
    }

    MiniString& operator+=(const char* s) {
        if (!s || !*s) return *this;
        size_t len = std::strlen(s);
        ensure_capacity(_size + len + 1);
        std::memcpy(_data + _size, s, len);
        _size += len;
        _data[_size] = '\0';
        return *this;
    }

    MiniString& operator+=(char ch) {
        ensure_capacity(_size + 2);
        _data[_size++] = ch;
        _data[_size] = '\0';
        return *this;
    }

    void push_back(char ch) {
        *this += ch;
    }

    char back() const {
        return _size ? _data[_size - 1] : '\0';
    }

    void pop_back() {
        if (_size == 0) return;
        --_size;
        _data[_size] = '\0';
    }

    int compare(const MiniString& other) const {
        return std::strcmp(c_str(), other.c_str());
    }

    // Simple search used by some code to trim trailing characters.
    size_t find_last_not_of(char ch) const {
        if (_size == 0) return static_cast<size_t>(-1);
        for (size_t i = _size; i > 0; --i) {
            if (_data[i - 1] != ch) return i - 1;
        }
        return static_cast<size_t>(-1);
    }

    MiniString substr(size_t pos, size_t len) const {
        if (pos > _size) pos = _size;
        if (pos + len > _size) len = _size - pos;
        MiniString out;
        if (len == 0) return out;
        out.ensure_capacity(len + 1);
        std::memcpy(out._data, _data + pos, len);
        out._size = len;
        out._data[len] = '\0';
        return out;
    }

private:
    void ensure_capacity(size_t needed) {
        if (needed <= _capacity) return;
        size_t new_cap = _capacity ? _capacity * 2 : 16;
        if (new_cap < needed) new_cap = needed;
        reserve(new_cap);
    }

    char* _data;
    size_t _size;
    size_t _capacity;
};

inline bool operator==(const MiniString& a, const MiniString& b) {
    return a.compare(b) == 0;
}

inline bool operator!=(const MiniString& a, const MiniString& b) {
    return !(a == b);
}

inline bool operator<(const MiniString& a, const MiniString& b) {
    return a.compare(b) < 0;
}

inline bool operator>(const MiniString& a, const MiniString& b) {
    return a.compare(b) > 0;
}

inline MiniString operator+(MiniString lhs, const MiniString& rhs) {
    lhs += rhs;
    return lhs;
}

inline MiniString operator+(MiniString lhs, const char* rhs) {
    lhs += rhs;
    return lhs;
}

inline MiniString operator+(const char* lhs, const MiniString& rhs) {
    MiniString s(lhs);
    s += rhs;
    return s;
}

// Minimal numeric formatting helpers used throughout the runtime.
inline MiniString MiniToString(int value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", value);
    return MiniString(buf);
}

inline MiniString MiniToString(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    return MiniString(buf);
}

