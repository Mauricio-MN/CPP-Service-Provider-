#include "user_normal.h"

namespace example {

int UserNormal::getId() const {
    return id;
}

void UserNormal::setId(int newId) {
    id = newId * 2;
}

} // namespace example
