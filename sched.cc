#include "sched.hh"
#include <list>
#include "mutex.hh"
#include <mutex>
#include "debug.hh"
#include "drivers/clockevent.hh"

namespace sched {

std::list<thread*> runqueue;

thread __thread * s_current;

elf::tls_data tls;

}

#include "arch-switch.hh"

namespace sched {

void schedule()
{
    thread* p = thread::current();
    if (!p->_waiting) {
        return;
    }
    // FIXME: a proper idle mechanism
    while (runqueue.empty()) {
        barrier();
    }
    thread* n = runqueue.front();
    runqueue.pop_front();
    assert(!n->_waiting);
    n->_on_runqueue = false;
    n->switch_to();
}

thread::thread(std::function<void ()> func, bool main)
    : _func(func)
    , _on_runqueue(!main)
    , _waiting(false)
{
    if (!main) {
        setup_tcb();
        init_stack();
        runqueue.push_back(this);
    } else {
        setup_tcb_main();
        s_current = this;
        switch_to_thread_stack();
        abort();
    }
}

thread::~thread()
{
    debug("thread dtor");
}

void thread::prepare_wait()
{
    _waiting = true;
}

void thread::wake()
{
    if (!_waiting) {
        return;
    }
    _waiting = false;
    if (!_on_runqueue) {
        _on_runqueue = true;
        runqueue.push_back(this);
    }
}

void thread::main()
{
    _func();
}

thread* thread::current()
{
    return sched::s_current;
}

void thread::wait()
{
    if (!_waiting) {
        return;
    }
    schedule();
}

void thread::stop_wait()
{
    _waiting = false;
}

thread::stack_info thread::get_stack_info()
{
    return stack_info { _stack, sizeof(_stack) };
}

timer_list timers;

timer_list::timer_list()
{
    clock_event->set_callback(this);
}

void timer_list::fired()
{
    timer& tmr = *_list.begin();
    tmr._expired = true;
    tmr._t.wake();
}

timer::timer(thread& t)
    : _t(t)
    , _expired()
{
}

timer::~timer()
{
    cancel();
}

void timer::set(u64 time)
{
    _time = time;
    // FIXME: locking
    timers._list.insert(*this);
    if (timers._list.iterator_to(*this) == timers._list.begin()) {
        clock_event->set(time);
    }
};

void timer::cancel()
{
    // FIXME: locking
    timers._list.erase(*this);
    _expired = false;
    // even if we remove the first timer, allow it to expire rather than
    // reprogramming the timer
}

bool timer::expired() const
{
    return _expired;
}

bool operator<(const timer& t1, const timer& t2)
{
    if (t1._time < t2._time) {
        return true;
    } else if (t1._time == t2._time) {
        return &t1 < &t2;
    } else {
        return false;
    }
}

void init(elf::program& prog)
{
    tls = prog.tls();
}

}
