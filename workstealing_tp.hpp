#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <deque>


class work_stealing_queue
{
private:
    typedef std::function<void()> data_type;
    std::deque<data_type> the_queue;                   
    mutable std::mutex the_mutex;
public:
    work_stealing_queue()
    {}
    work_stealing_queue(const work_stealing_queue& other)=delete;
    work_stealing_queue& operator=(
        const work_stealing_queue& other)=delete;

    void push(data_type data)                          
    {
        std::lock_guard<std::mutex> lock(the_mutex);
        the_queue.push_front(std::move(data));
    }
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(the_mutex);
        return the_queue.empty();
    }
    bool try_pop(data_type& res)                       
    {
        std::lock_guard<std::mutex> lock(the_mutex);
        if(the_queue.empty())
        {
            return false;
        }
        res=std::move(the_queue.front());
        the_queue.pop_front();
        return true;
    }
    bool try_steal(data_type& res)                     
    {
        std::lock_guard<std::mutex> lock(the_mutex);
        if(the_queue.empty())
        {
            return false;
        }
        res=std::move(the_queue.back());
        the_queue.pop_back();
        return true;
    }
};

template<typename T>
class threadsafe_queue
{
private:
    struct node
    {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };
    
    std::mutex head_mutex;
    std::unique_ptr<node> head;
    std::mutex tail_mutex;
    node* tail;

    std::unique_ptr<node> pop_head()
    {
        std::unique_ptr<node>  old_head=std::move(head);
        head=std::move(old_head->next);
        return old_head;
    }

    node* get_tail()
    {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    }

    std::unique_ptr<node> try_pop_head()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        return pop_head();
    }

    std::unique_ptr<node> try_pop_head(T& value)
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        value=std::move(*head->data);
        return pop_head();
    }

    

public:
    threadsafe_queue():
            head(new node),tail(head.get())
        {}

    threadsafe_queue(const threadsafe_queue& other)=delete;
    threadsafe_queue& operator=(const threadsafe_queue& other)=delete;

    void push(T new_value)
    {
        std::shared_ptr<T> new_data(
            std::make_shared<T>(std::move(new_value)));
        std::unique_ptr<node> p(new node);
        {
            std::lock_guard<std::mutex> tail_lock(tail_mutex);
            tail->data=new_data;
            node* const new_tail=p.get();
            tail->next=std::move(p);
            tail=new_tail;
        }
    }
    std::shared_ptr<T> try_pop()
    {
        std::unique_ptr<node> const old_head=try_pop_head();
        return old_head?old_head->data:std::shared_ptr<T>();
    }

    bool try_pop(T& value)
    {
        std::unique_ptr<node> const old_head=try_pop_head(value);
        return (bool) old_head;
    }

    bool empty()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        return (head==get_tail());
    }
};


class ThreadPool{
public:
    typedef std::function<void()> task_type;
    bool pop_task_from_local_queue(task_type& task)
    {
        return local_work_queue && local_work_queue->try_pop(task);
    }
    bool pop_task_from_pool_queue(task_type& task)
    {
        return pool_work_queue.try_pop(task);
    }
    bool pop_task_from_other_thread_queue(task_type& task)              
    {
        for(unsigned i=0;i<queues.size();++i)
        {
            unsigned const index=(my_index+i+1)%queues.size();          
            if(queues[index]->try_steal(task))
            {
                // std::cerr<<"workstolen"<<std::endl;
                return true;
            }
        }
        return false;
    }

    inline ThreadPool(size_t threads)
        : stop(false)
    {

        for(unsigned i=0;i<threads; ++i)
        {
            queues.push_back(std::unique_ptr<work_stealing_queue>(  
                                    new work_stealing_queue));
        }
        for(size_t i = 0;i<threads;++i)
            workers.emplace_back(
                [this, i]
                {
                    my_index=i;
                    local_work_queue=queues[my_index].get();
                   
                    
                    std::function<void()> task;
                   
                    for(;;)
                    {
                        if(run_pending_task(task))
                            task();
                        else if(stop)
                            return;
                        else
                            std::this_thread::yield();
                    }
                }
            );
    }

    template<typename FunctionType, class... Args>
    decltype(auto) submit(FunctionType&& f, Args&&... args)
    {
        using result_type = decltype(f(args...));
        auto task =  std::make_shared<std::packaged_task<result_type()>>(std::bind(
                                                                        std::forward<FunctionType>(f), std::forward<Args>(args)...));
        std::future<result_type> fut(task->get_future());
        auto t = [task](){(*task)();};
        if(local_work_queue)                                 
        {
            local_work_queue->push(std::move(t));
        }
        else
        {
            pool_work_queue.push(std::move(t));           
        }
        return fut;
    }



    // the destructor joins all threads
    inline ~ThreadPool()
    {
        stop = true;
        for(std::thread &worker: workers)
            worker.join();
    }

    bool run_pending_task(task_type& task)
    {
        if(pop_task_from_local_queue(task) ||                           
           pop_task_from_pool_queue(task) ||                            
           pop_task_from_other_thread_queue(task))                      
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    void run_pending_task()
    {
        task_type task;
        if(pop_task_from_local_queue(task) ||                          
           pop_task_from_pool_queue(task) ||                            
           pop_task_from_other_thread_queue(task))                      
        {
            task();
        }
        else
        {
            std::this_thread::yield();
        }
    }
    std::vector< std::thread > workers;
    threadsafe_queue<std::function<void()> > pool_work_queue;
    std::vector<std::unique_ptr< work_stealing_queue> > queues;
    inline static thread_local work_stealing_queue* local_work_queue;        
    inline static thread_local unsigned my_index;
  
    bool stop;
};


