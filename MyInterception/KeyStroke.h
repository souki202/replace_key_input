#pragma once
#include <iostream>

struct KeyStroke {
    using KeyCodeType = unsigned short;
    enum class KeyStateType {
        NORMAL, ALTERNATE_KEY, INVALID
    };

    KeyStroke() {
        code = 0;
        state = KeyStateType::NORMAL;
    }
    KeyStroke(KeyCodeType code, KeyStateType state) {
        this->code = code;
        this->state = state;
    }

    friend std::ostream& operator << (std::ostream& stream, const KeyStroke& k);

    bool operator == (const KeyStroke& rhs) const noexcept {
        return code == rhs.code && state == rhs.state;
    }

    bool operator != (const KeyStroke& rhs) const noexcept {
        return !operator==(rhs);
    }

    KeyCodeType code;
    KeyStateType state;
};

std::ostream& operator << (std::ostream& stream, const KeyStroke& k) {
    stream << "code: " << k.code << " state: " << static_cast<int>(k.state) * 2;
    return stream;
}

struct KeyStrokeHash {
    size_t operator() (const KeyStroke& k) const { return k.code + k.code * static_cast<size_t>(k.state); };
};