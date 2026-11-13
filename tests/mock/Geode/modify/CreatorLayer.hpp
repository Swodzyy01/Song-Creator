#pragma once
#include "../Geode.hpp"
// mock: `class $modify(Name, CreatorLayer)` becomes plain inheritance
#define $modify(Name, Base) Name : public Base
