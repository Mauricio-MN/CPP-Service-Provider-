#ifndef EXAMPLE_USER_NORMAL_H
#define EXAMPLE_USER_NORMAL_H

#include "iuser.h"

namespace example {

class UserNormal final : public IUser {
private:
    int id = 0;

public:
    int getId() const override;
    void setId(int newId) override;
};

} // namespace example

#endif
