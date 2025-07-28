
#pragma once
#include <type_traits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


// :: Trait to detect free from_json(json, T&) function
// ****************************************************


namespace detail {
    template <typename T>
    struct has_from_json_impl {
        template <typename U>
        static auto test(int) -> decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<U&>()), std::true_type{});
        template <typename>
        static std::false_type test(...);
        static constexpr bool value = decltype(test<T>(0))::value;
    };
    template <typename T>
    struct has_to_json_impl {
        template <typename U>
        static auto test(int) -> decltype(to_json(std::declval<nlohmann::json&>(), std::declval<const U&>()), std::true_type{});
        template <typename>
        static std::false_type test(...);
        static constexpr bool value = decltype(test<T>(0))::value;
    };
    template <typename T>
    struct has_serialize_impl {
        template <typename U>
        static auto test(int) -> decltype(std::declval<const U&>().serialize(), std::true_type{});
        template <typename>
        static std::false_type test(...);
        static constexpr bool value = decltype(test<T>(0))::value;
    };
}

// Public traits
template <typename T>
using has_from_json = std::integral_constant<bool, detail::has_from_json_impl<T>::value>;

template <typename T>
using has_to_json = std::integral_constant<bool, detail::has_to_json_impl<T>::value>;

template <typename T>
using has_serialize = std::integral_constant<bool, detail::has_serialize_impl<T>::value>;

// Trait to check if a type has a static schema() method
namespace detail {
    template<typename T>
    struct has_schema_impl {
        template<typename U>
        static auto test(int) -> decltype(U::schema(), std::true_type{});
        template<typename>
        static std::false_type test(...);
        static constexpr bool value = decltype(test<T>(0))::value;
    };
}

template<typename T>
using has_schema = std::integral_constant<bool, detail::has_schema_impl<T>::value>;