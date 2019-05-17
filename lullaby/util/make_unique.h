/*
Copyright 2017-2019 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef LULLABY_UTIL_MAKE_UNIQUE_H_
#define LULLABY_UTIL_MAKE_UNIQUE_H_

#include <memory>
#include <type_traits>

namespace lull {


// MakeUnique for scalar allocations.
template <typename T, typename... Args>
typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
MakeUnique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// MakeUnique for array allocations (eg MakeUnique<int[]>(10)).
template <typename T>
typename std::enable_if<std::is_array<T>::value, std::unique_ptr<T>>::type
MakeUnique(size_t count) {
  return std::unique_ptr<T>(new typename std::remove_extent<T>::type[count]);
}

}  // namespace lull

#endif  // LULLABY_UTIL_MAKE_UNIQUE_H_
