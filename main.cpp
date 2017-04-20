#include <iostream>
#include <memory>
#include <mutex>
#include <future>
#include <vector>
#include <algorithm>

template<typename T>
class threadsafe_queue {
private:
    struct node {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };
    std::mutex head_mutex, tail_mutex;
    std::unique_ptr<node> head;
    node *tail;

    node *get_tail() {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    }

    std::unique_ptr<node> pop_head() {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if (head.get() == get_tail())
            return nullptr;
        std::unique_ptr<node> old_head = std::move(head);
        head = std::move(old_head->next);
        return old_head;
    }

public:
    threadsafe_queue() : head(new node), tail(head.get()) {}

    threadsafe_queue(const threadsafe_queue &other) = delete;

    threadsafe_queue &operator=(const threadsafe_queue &other)= delete;

    std::shared_ptr<T> try_pop() {
        std::unique_ptr<node> old_head = pop_head();
        return old_head ? old_head->data : std::shared_ptr<T>();
    }

    void push(T new_value) {
        std::shared_ptr<T> new_data(std::make_shared<T>(std::move(new_value)));
        std::unique_ptr<node> p(new node);
        node *const new_tail = p.get();
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        tail->data = new_data;
        tail->next = std::move(p);
        tail = new_tail;
    }
};

void client_func(
        threadsafe_queue<std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>>> *queue, int num, int k) {

    auto get_time = std::chrono::steady_clock::now;
    std::chrono::milliseconds timeout(5000);
    decltype(get_time()) start, end;
    double total_time = 0;
    int count_done = 0;
    int count_undone = 0;
    std::cout << "Client" << num << "start\n";
    while (count_done <= count_undone + k) {
        std::promise<int> *prom = new std::promise<int>();
        std::shared_ptr<bool> job = std::make_shared<bool>(false);
        std::future<int> fut = prom->get_future();
        queue->push(std::make_pair(job, std::shared_ptr<std::promise<int>>(prom)));

        if (fut.wait_for(timeout) == std::future_status::timeout) {
            std::cout << "Client" << num << " timeout\n";
            break;
        }

        int t = fut.get();

        start = get_time();
        double elapsed;
        do {
            end = get_time();
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        } while (!*job && elapsed < t);
        if (*job) {
            count_done++;
            std::cout << "Client" << num << " done\n";
        } else {
            *job = true;
            count_undone++;
            std::cout << "Client" << num << " undone\n";
        }
        total_time += elapsed;
    }
    std::cout << "Client" << num << " end   Average time = " << total_time / (count_done + count_undone) << "msec.\n";
}

void server_func(threadsafe_queue<std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>>> *queue, int num,
                 int k,
                 double frec) {
    double timeout = 5000;
    int t = (int) (1.0 / frec);
    double total_time = 0;
    int count_done = 0;
    int count_undone = 0;
    unsigned seed = (unsigned) time(NULL);
    auto get_time = std::chrono::steady_clock::now;
    decltype(get_time()) start, end, cur_wait;
    std::cout << "Server" << num << " start\n";
    end = get_time();
    while (count_undone <= count_done + k) {
        std::shared_ptr<std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>>> ptr = queue->try_pop();
        if (ptr) {
            std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>> *data = ptr.get();
            std::shared_ptr<std::promise<int>> prom = data->second;
            bool *job = data->first.get();
            prom->set_value(t);

            start = get_time();
            std::this_thread::sleep_for(std::chrono::milliseconds(rand_r(&seed) % t + t / 2));
            end = get_time();
            if (!*job) {
                std::cout << "Server" << num << " done\n";
                *job = true;
                count_done++;
            } else {
                count_undone++;
                std::cout << "Server" << num << " undone\n";
            }

            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            total_time += elapsed;
        }
        cur_wait = get_time();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(cur_wait - end).count() > timeout) {
            std::cout << "Server" << num << " timeout\n";
            break;
        }
    }
    std::cout << "Server" << num << " end   Average time = " << total_time / (count_done + count_undone) << "msec.\n";
}

int main(int argc, char **argv) {

    if (argc < 6) {
        std::cout << "Not enough args\n";
        return 1;
    }
    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    int q = atoi(argv[3]);
    double freq = atof(argv[4]);
    int k = atoi(argv[5]);

    threadsafe_queue<std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>>> *queue =
            new threadsafe_queue<std::pair<std::shared_ptr<bool>, std::shared_ptr<std::promise<int>>>>();

    std::vector<std::thread> clients;
    for (int i = 0; i < n; ++i) {
        clients.push_back(std::thread(client_func, queue, i, k));
    }

    std::vector<std::thread> servers;
    for (int i = 0; i < m; ++i) {
        clients.push_back(std::thread(server_func, queue, i, k, freq));
    }

    std::for_each(clients.begin(), clients.end(), std::mem_fn(&std::thread::join));
    std::for_each(servers.begin(), servers.end(), std::mem_fn(&std::thread::join));

    return 0;
}