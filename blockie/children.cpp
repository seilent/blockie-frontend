#include "children.h"

#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace fd {

pid_t Children::spawn(const std::vector<std::string>& argv, Done done, int* out)
{
    if (out)
        *out = -1;
    if (argv.empty())
        return -1;
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    int p[2] = {-1, -1};
    if (out && pipe2(p, O_CLOEXEC) != 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0) {
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);
        if (out && dup2(p[1], STDOUT_FILENO) < 0)
            _exit(127);
        close_range(3, ~0U, 0);
        execv(args[0], args.data());
        _exit(127);
    }
    if (out) {
        close(p[1]);
        if (pid < 0) {
            close(p[0]);
            return -1;
        }
        fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
        *out = p[0];
    }
    if (pid < 0)
        return -1;
    done_[pid] = std::move(done);
    return pid;
}

bool Children::reaped(pid_t pid, int status)
{
    auto it = done_.find(pid);
    if (it == done_.end())
        return false;
    Done d = std::move(it->second);
    done_.erase(it);
    if (d)
        d(status);
    return true;
}

}
