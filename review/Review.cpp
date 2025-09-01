#include <string_view>
#include <concepts>
#include <memory>
#include <stack>
#include <iostream>
#include <type_traits>

// restore actions should be nothrow... otherwise things
// get complicated really quickly ;)
template <typename F>
concept nothrow_invocable = requires(F f) {
    { f() } noexcept;
};

class IRestore
{
public:
    virtual ~IRestore() = default;
    virtual void Restore() noexcept = 0;
};

template <typename fn_t>
    requires nothrow_invocable<fn_t>
class RestoreImpl final : public IRestore
{
public:
    RestoreImpl(fn_t fn)
        : m_fn{fn}
    {
    }

    void Restore() noexcept override
    {
        m_fn();
    }

private:
    fn_t m_fn;
};

class RestoreStack
{
public:
    template <typename fn_t>
        requires nothrow_invocable<fn_t>
    void Push(fn_t fn)
    {
        m_stack.push(std::move(std::make_unique<RestoreImpl<fn_t>>(fn)));
    }

    void Pop()
    {
        std::unique_ptr<IRestore> top{std::move(m_stack.top())};
        m_stack.pop();
        top->Restore();
    }

private:
    std::stack<std::unique_ptr<IRestore>> m_stack;
};

class SomeClass
{
public:
    // this is not strictly necessary, but makes it easier
    // to maintain which members should participate in 
    // restoring. (Easy to add more data later without refactoring
    // in a lot of places).
    struct RestorableState
    {
        std::string name;
        int x;
        int y;
    };

    SomeClass(const RestorableState& state) noexcept :
        m_state{state}
    {
    }

    const RestorableState& GetRestorableState() const noexcept
    {
        return m_state;
    }

    void set(int x, int y)
    {
        m_state.x = x;
        m_state.y = y;
    }

private:
    RestorableState m_state;

};

std::ostream& operator<<(std::ostream& os, const SomeClass& s)
{
    auto& state = s.GetRestorableState();
    os << state.name << " = { x = " << state.x << ", y = " << state.y << " }";
    return os;
}

int main()
{
    RestoreStack restore;

    SomeClass instance1{{"instance1", 1, 1}};
    
    std::cout << instance1 << "\n";
    
    // the lambda can now do anything to restore a state   
    restore.Push(
        [&, state = instance1.GetRestorableState()]() noexcept
        {
            // here is your call to parameterized constructor
            instance1 = std::move(SomeClass{state});
        });

    std::cout << "Set\n";
    instance1.set(3, 3);
    std::cout << instance1 << "\n";

    std::cout << "Restore\n";
    restore.Pop();
    std::cout << instance1 << "\n";
}