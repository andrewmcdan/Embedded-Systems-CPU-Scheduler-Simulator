#pragma once

#include <queue>
#include <vector>
#include <deque>
#include <algorithm>
#include <functional>
#include <optional>
#include "Task.h"

/**
 * @brief RunQueue encapsulates task storage for the scheduler.
 * It supports various queueing modes (FIFO, priority, or custom comparator).
 */
class RunQueue {
public:
    using Comparator = std::function<bool(Task*, Task*)>;

    /**
     * @brief Construct a RunQueue with an optional custom comparator.
     * If no comparator is provided, tasks are treated in FIFO order.
     */
    explicit RunQueue(Comparator comp = nullptr)
        : comparator_(std::move(comp)) {}

    /**
     * @brief Add a task to the queue.
     */
    void enqueue(Task* task) {
        if (!task) return;
        if (comparator_) {
            // Priority or custom-ordered queue
            auto it = std::find_if(tasks_.begin(), tasks_.end(),
                [&](Task* t) { return comparator_(task, t); });
            tasks_.insert(it, task);
        } else {
            // Default FIFO
            tasks_.push_back(task);
        }
    }

    /**
     * @brief Retrieve and remove the next task.
     * @return Pointer to the next task, or nullptr if empty.
     */
    Task* dequeue() {
        if (tasks_.empty()) return nullptr;
        Task* t = tasks_.front();
        tasks_.pop_front();
        return t;
    }

    /**
     * @brief Peek at the next task without removing it.
     */
    Task* peek() const {
        return tasks_.empty() ? nullptr : tasks_.front();
    }

    /**
     * @brief Check if the queue is empty.
     */
    bool empty() const { return tasks_.empty(); }

    /**
     * @brief Get current queue size.
     */
    size_t size() const { return tasks_.size(); }

    /**
     * @brief Remove a specific task (if present).
     */
    void remove(Task* task) {
        tasks_.erase(
            std::remove(tasks_.begin(), tasks_.end(), task),
            tasks_.end()
        );
    }

    /**
     * @brief Clear all tasks from the queue.
     */
    void clear() { tasks_.clear(); }

    /**
     * @brief Sort the queue using the current comparator (if any).
     */
    void sort() {
        if (comparator_) {
            std::sort(tasks_.begin(), tasks_.end(), comparator_);
        }
    }

    /**
     * @brief Retrieve the underlying container (for debugging or analysis).
     */
    const std::deque<Task*>& data() const { return tasks_; }

private:
    std::deque<Task*> tasks_;
    Comparator comparator_;
};
