#ifndef EXAMPLE_IUSER_H
#define EXAMPLE_IUSER_H

namespace example {

// The client and provider share this contract, not the concrete implementations.
class IUser {
public:
    virtual ~IUser() = default;
    virtual int getId() const = 0;
    virtual void setId(int id) = 0;
};

} // namespace example

#endif
