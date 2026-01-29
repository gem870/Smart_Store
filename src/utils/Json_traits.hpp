
#pragma once
#include <type_traits>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


// :: Trait to detect free from_json(json, T&) function
// ****************************************************

/**
 * @file Json_traits.hpp
 * @brief Type trait system for JSON serialization detection and schema management.
 * 
 * @details This module provides compile-time type traits (SFINAE-based) to detect 
 * and validate JSON serialization capabilities at compile time. It forms the foundation 
 * of the SmartStore framework's polymorphic serialization system.
 * 
 * **Core Features:**
 * - Detection of from_json() free functions via @ref has_from_json
 * - Detection of to_json() free functions via @ref has_to_json
 * - Detection of serialize() member functions via @ref has_serialize
 * - Detection of schema() static methods via @ref has_schema
 * 
 * **Technical Foundation:**
 * 
 * This implementation uses:
 * 1. **SFINAE (Substitution Failure Is Not An Error)**: Template substitution failures 
 *    don't cause compilation errors but instead select alternative overloads.
 * 2. **Expression SFINAE**: Checks if expressions are valid using decltype() in function return types
 * 3. **Integral Constants**: Results compile into std::integral_constant<bool> for type-safe boolean traits
 * 
 * **Why Type Traits Matter:**
 * 
 * Without these traits, ItemManager would need to:
 * - Store type-specific serializers manually (tedious and error-prone)
 * - Write serialization logic multiple times for each type
 * - Lose compile-time validation of serialization capability
 * 
 * With traits, we can:
 * - Enable/disable features based on type capabilities
 * - Generate appropriate code only for serializable types
 * - Provide clear compile-time errors if serialization not supported
 * - Maintain single source of truth for type requirements
 * 
 * @section CompileTimeVerification
 * 
 * These traits are evaluated at **compile time**, meaning:
 * - Zero runtime overhead (traits are erased after compilation)
 * - Compile-time errors if type doesn't support required operations
 * - Optimization opportunities through partial specialization
 * 
 * **Usage Example:**
 * ```cpp
 * // In ItemManager, check if type T is serializable:
 * if constexpr (has_serialize<T>::value) {
 *     // Only compiled if T has serialize() method
 * } else if constexpr (has_from_json<T>::value) {
 *     // Only compiled if T has from_json() function
 * }
 * ```
 * 
 * @author Victor
 * @date 2025
 */

/**
 * @namespace detail
 * @brief Internal namespace for SFINAE trait implementations.
 * 
 * Contains implementation details for type trait detection. Users should not
 * directly interact with classes/types in this namespace; use the public 
 * alias templates instead (@ref has_from_json, @ref has_to_json, etc).
 * 
 * @details Traits in this namespace use advanced C++ metaprogramming:
 * 
 * **SFINAE Mechanism Explained:**
 * 1. Two test() functions are defined with identical names
 * 2. One accepts int (higher priority in overload resolution)
 * 3. Other accepts ... (fallback, lower priority)
 * 4. First overload uses decltype() with the expression to check
 * 5. If expression is valid -> first overload selected -> returns std::true_type
 * 6. If expression is invalid (substitution fails) -> fallback selected -> returns std::false_type
 * 7. Result stored in ::value compile-time constant
 * 
 * **Example Signature:**
 * ```cpp
 * template<typename U>
 * static auto test(int) -> decltype(
 *     from_json(std::declval<const json&>(), std::declval<U&>()), 
 *     std::true_type{}
 * );
 * ```
 * - `std::declval<const json&>()`: Creates dummy json object without construction
 * - `std::declval<U&>()`: Creates dummy U reference without construction  
 * - `decltype(expr)`: Returns the type of the expression
 * - Comma operator: Expression sequence returns last element (std::true_type{})
 * 
 * @note This is an internal implementation detail namespace
 */
namespace detail 
{
     /**
     * @struct has_from_json_impl
     * @brief SFINAE trait implementation for detecting free function from_json().
     * 
     * @tparam T The type to check for from_json support
     * 
     * **Purpose:** Determines whether a free function `from_json(const json&, T&)` exists.
     * 
     * **Implementation Details:**
     * 
     * Two overloaded test() static functions are defined:
     * 1. test(int): Returns std::true_type if `from_json(json, T&)` expression is valid
     * 2. test(...): Fallback that returns std::false_type
     * 
     * **How SFINAE Works Here:**
     * 
     * ```cpp
     * template<typename U>
     * static auto test(int) -> decltype(
     *     from_json(std::declval<const nlohmann::json&>(), std::declval<U&>()), 
     *     std::true_type{}
     * );
     * ```
     * 
     * When instantiating test<T>(0):
     * - Tries to evaluate: `from_json(json, T&)`
     * - If valid expression: Returns std::true_type via comma operator
     * - If invalid (no from_json): Substitution fails → tries test(...) instead
     * - Result stored in ::value compile-time constant
     * 
     * **Usage in ItemManager:**
     * ```cpp
     * if constexpr (has_from_json<MyType>::value) {
     *     // Can deserialize MyType from JSON
     * }
     * ```
     * 
     * @see @ref has_from_json Public alias template
     */
    template <typename T>
    struct has_from_json_impl 
    {
        /**
         * @brief Overload 1: Detects if free function from_json() exists.
         * 
         * This overload is preferred when resolving test(0) because int is a better match
         * than the ellipsis (...) in the fallback overload.
         * 
         * @tparam U Type to check (deduced as T)
         * @return std::true_type if `from_json(const json&, U&)` is a valid expression
         * 
         * @note If the decltype expression is invalid, this overload is discarded via SFINAE
         *       and the fallback test(...) is used instead.
         */
        template <typename U>
        static auto test(int) -> decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<U&>()), std::true_type{});

        /**
         * @brief Overload 2: Fallback if from_json not found.
         * 
         * This overload is selected when the first overload's decltype fails.
         * Has lower priority due to ellipsis parameter matching.
         * 
         * @return std::false_type indicating from_json() doesn't exist
         */
        template <typename>
        static std::false_type test(...);

        /**
         * @brief Compile-time boolean constant for trait result.
         * 
         * - true if from_json(const json&, T&) is callable
         * - false if from_json() does not exist
         * 
         * Evaluation: `decltype(test<T>(0))` produces either std::true_type or std::false_type,
         * and `::value` extracts the boolean constant.
         */
        static constexpr bool value = decltype(test<T>(0))::value;
    };
    
    /**
     * @struct has_to_json_impl
     * @brief SFINAE trait implementation for detecting free function to_json().
     * 
     * @tparam T The type to check for to_json support
     * 
     * **Purpose:** Determines whether a free function `to_json(json&, const T&)` exists.
     * 
     * **Serialization Pattern:**
     * 
     * This trait detects the inverse operation of from_json(). While from_json deserializes
     * data FROM JSON into a C++ object, to_json serializes FROM a C++ object INTO JSON.
     * 
     * **Expected Function Signature:**
     * ```cpp
     * void to_json(nlohmann::json& j, const MyType& obj) {
     *     j = nlohmann::json{{"field1", obj.field1}, {"field2", obj.field2}};
     * }
     * ```
     * 
     * **SFINAE Detection:**
     * 
     * Checks if calling `to_json(json_ref, object_ref)` is valid at compile time.
     * - If valid: returns std::true_type
     * - If invalid: Substitution fails → fallback returns std::false_type
     * 
     * **Typical Usage in ItemManager:**
     * ```cpp
     * if constexpr (has_to_json<MyType>::value) {
     *     // Can serialize MyType to JSON
     * }
     * ```
     * 
     * @see @ref has_to_json Public alias template
     */
    template <typename T>
    struct has_to_json_impl 
    {
        /**
         * @brief Overload 1: Detects if free function to_json() exists.
         * 
         * Checks if `to_json(json&, const T&)` is a callable expression.
         * 
         * @tparam U Type to check (deduced as T)
         * @return std::true_type if to_json is callable for type U
         */
        template <typename U>
        static auto test(int) -> decltype(to_json(std::declval<nlohmann::json&>(), std::declval<const U&>()), std::true_type{});

        /**
         * @brief Overload 2: Fallback if to_json not found.
         * 
         * Selected when first overload's SFINAE check fails.
         * 
         * @return std::false_type indicating to_json() doesn't exist
         */
        template <typename>
        static std::false_type test(...);
        /**
         * @brief Compile-time boolean constant for to_json availability.
         * 
         * - true if `to_json(json&, const T&)` can be called
         * - false otherwise
         */
        static constexpr bool value = decltype(test<T>(0))::value;
    };

      /**
     * @struct has_serialize_impl
     * @brief SFINAE trait implementation for detecting member function serialize().
     * 
     * @tparam T The type to check for serialize() member function
     * 
     * **Purpose:** Determines whether a member method `T::serialize() const` exists.
     * 
     * **Member Function Pattern:**
     * 
     * Unlike from_json/to_json which are free functions, serialize() is a member method:
     * ```cpp
     * class MyItem {
     * public:
     *     json serialize() const {
     *         return json{{"field1", field1}, {"field2", field2}};
     *     }
     * private:
     *     int field1;
     *     std::string field2;
     * };
     * ```
     * 
     * **SFINAE Detection:**
     * 
     * Checks if calling `obj.serialize()` (const method) is valid at compile time.
     * 
     * **Priority in ItemManager:**
     * 
     * Serialization methods are checked in priority order:
     * 1. Does type have serialize() member? → Use it
     * 2. Does type have to_json() free function? → Use it
     * 3. Is type a BaseItem? → Use BaseItem::serialize()
     * 4. Otherwise → ERROR: Type not serializable
     * 
     * **Usage Example:**
     * ```cpp
     * if constexpr (has_serialize<T>::value) {
     *     // Can call T::serialize()
     *     auto json_obj = item.serialize();
     * }
     * ```
     * 
     * @see @ref has_serialize Public alias template
     */
    template <typename T>
    struct has_serialize_impl 
    {
         /**
         * @brief Overload 1: Detects if serialize() member function exists.
         * 
         * Checks if const method `T::serialize()` can be called on object of type T.
         * 
         * @tparam U Type to check (deduced as T)
         * @return std::true_type if T::serialize() const is callable
         */
        template <typename U>
        static auto test(int) -> decltype(std::declval<const U&>().serialize(), std::true_type{});

         
        /**
         * @brief Overload 2: Fallback if serialize not found.
         * 
         * Selected when first overload's decltype fails (serialize not found).
         * 
         * @return std::false_type indicating serialize() doesn't exist
         */
        template <typename>
        static std::false_type test(...);

        /**
         * @brief Compile-time boolean constant for serialize() availability.
         * 
         * - true if T has member function `serialize() const`
         * - false if serialize() not found
         */
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

 /**
     * @struct has_schema_impl
     * @brief SFINAE trait implementation for detecting static schema() method.
     * 
     * @tparam T The type to check for schema support
     * 
     * **Purpose:** Determines whether a static method `T::schema()` exists.
     * 
     * **Schema Concept:**
     * 
     * A JSON schema describes the structure and constraints of data. The schema() method
     * should return a JSON Schema representation of the type's structure:
     * 
     * ```cpp
     * class User {
     * public:
     *     static json schema() {
     *         return json{
     *             {"type", "object"},
     *             {"properties", {
     *                 {"name", {{"type", "string"}}},
     *                 {"age", {{"type", "integer"}}},
     *                 {"email", {{"type", "string"}, {"format", "email"}}}
     *             }},
     *             {"required", {"name", "age"}}
     *         };
     *     }
     * private:
     *     std::string name;
     *     int age;
     *     std::string email;
     * };
     * ```
     * 
     * **SFINAE Detection:**
     * 
     * Checks if the static method `T::schema()` can be called at compile time.
     * This is a static method (not instance method), so detection differs from serialize().
     * 
     * **Use Cases for Schema:**
     * 
     * 1. **Validation**: Check if JSON data matches expected structure before deserialization
     * 2. **Documentation**: Provide machine-readable type documentation
     * 3. **UI Generation**: Automatically generate forms from schema
     * 4. **Interoperability**: Share structure information with other systems
     * 
     * **Usage in ItemManager:**
     * ```cpp
     * if constexpr (has_schema<T>::value) {
     *     // Can retrieve schema for validation
     *     auto type_schema = T::schema();
     * }
     * ```
     * 
     * @see @ref has_schema Public alias template
     */
namespace detail 
{
    /**
         * @brief Overload 1: Detects if static schema() exists.
         * 
         * Returns std::true_type if T::schema() is valid expression.
         * The trailing comma operator returns second operand (std::true_type{}).
         * 
         * **Comma Operator Explanation:**
         * 
         * In C++, the comma operator evaluates both expressions and returns the second:
         * ```cpp
         * (expr1, expr2)  // evaluates expr1, discards result, returns expr2
         * ```
         * 
         * Usage here:
         * ```cpp
         * decltype(U::schema(), std::true_type{})
         * // Evaluates U::schema() for validity
         * // Returns std::true_type{} as the decltype result
         * // If U::schema() is invalid → substitution fails → SFINAE fallback used
         * ```
         * 
         * @tparam U Type parameter to check
         * @return std::true_type if T::schema() valid expression
         */
    template<typename T>
    struct has_schema_impl 
    {
         /**
         * @brief Overload 1: Detects if static schema() exists.
         * 
         * Returns std::true_type if T::schema() is valid expression.
         * The trailing comma operator returns second operand (std::true_type{}).
         * 
         * **Comma Operator Explanation:**
         * 
         * In C++, the comma operator evaluates both expressions and returns the second:
         * ```cpp
         * (expr1, expr2)  // evaluates expr1, discards result, returns expr2
         * ```
         * 
         * Usage here:
         * ```cpp
         * decltype(U::schema(), std::true_type{})
         * // Evaluates U::schema() for validity
         * // Returns std::true_type{} as the decltype result
         * // If U::schema() is invalid → substitution fails → SFINAE fallback used
         * ```
         * 
         * @tparam U Type parameter to check
         * @return std::true_type if T::schema() valid expression
         */
        template<typename U>
        static auto test(int) -> decltype(U::schema(), std::true_type{});

         /**
         * @brief Overload 2: Fallback if schema not found.
         * 
         * Returns std::false_type when U::schema() substitution fails.
         * This overload is selected when the primary template can't be instantiated.
         */
        template<typename>
        static std::false_type test(...);
         /**
         * @brief Compile-time boolean for schema availability.
         * 
         * true if static schema() exists, false otherwise.
         */
        static constexpr bool value = decltype(test<T>(0))::value;
    };
}

template<typename T>
using has_schema = std::integral_constant<bool, detail::has_schema_impl<T>::value>;
