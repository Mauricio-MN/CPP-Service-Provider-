#include "user_service.h"

#include "request_types.h"
#include "user_normal.h"
#include "user_staff.h"

#include <stdexcept>

namespace example {

std::shared_ptr<IUser> UserService::getNewInstance(std::type_info& requester) {
    if (requester == typeid(StaffRequest)) {
        return std::make_shared<UserStaff>();
    }
    if (requester == typeid(NormalRequest)) {
        return std::make_shared<UserNormal>();
    }

    throw std::invalid_argument("No IUser implementation for this requester");
}

} // namespace example
