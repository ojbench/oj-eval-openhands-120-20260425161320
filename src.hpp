#pragma once

#include "Task.hpp"
#include <vector>

class TaskNode {
    friend class TimingWheel;
    friend class Timer;
public:
    TaskNode() : task(nullptr), next(nullptr), prev(nullptr), time(0) {}
    TaskNode(Task* t, int tm) : task(t), next(nullptr), prev(nullptr), time(tm) {}

private:
    Task* task;
    TaskNode* next, *prev;
    int time;
};

class TimingWheel {
    friend class Timer;
public:
    TimingWheel(size_t size, size_t interval) : size(size), interval(interval), current_slot(0) {
        slots = new TaskNode*[size];
        for (size_t i = 0; i < size; ++i) {
            slots[i] = nullptr;
        }
    }
    
    ~TimingWheel() {
        delete[] slots;
    }
    
    void addTaskNode(TaskNode* node, size_t slot_index) {
        if (slots[slot_index] == nullptr) {
            slots[slot_index] = node;
            node->next = node;
            node->prev = node;
        } else {
            TaskNode* head = slots[slot_index];
            node->next = head;
            node->prev = head->prev;
            head->prev->next = node;
            head->prev = node;
        }
    }
    
    void removeTaskNode(TaskNode* node) {
        if (node->next == node) {
            // Only node in the list
            for (size_t i = 0; i < size; ++i) {
                if (slots[i] == node) {
                    slots[i] = nullptr;
                    break;
                }
            }
        } else {
            // Multiple nodes in the list
            for (size_t i = 0; i < size; ++i) {
                if (slots[i] == node) {
                    slots[i] = node->next;
                }
            }
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }
        node->next = nullptr;
        node->prev = nullptr;
    }
    
    std::vector<TaskNode*> advance() {
        current_slot = (current_slot + 1) % size;
        
        std::vector<TaskNode*> ready_tasks;
        
        TaskNode* node = slots[current_slot];
        if (node) {
            TaskNode* start = node;
            do {
                TaskNode* next = node->next;
                ready_tasks.push_back(node);
                node = next;
            } while (node != start);
        }
        
        // Clear the slot
        slots[current_slot] = nullptr;
        for (TaskNode* task_node : ready_tasks) {
            task_node->next = nullptr;
            task_node->prev = nullptr;
        }
        
        return ready_tasks;
    }
    
private:
    const size_t size, interval;
    size_t current_slot;
    TaskNode** slots;
};

class Timer {
public:    
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    Timer() : second_wheel(60, 1), minute_wheel(60, 60), hour_wheel(24, 3600) {}

    ~Timer() {
        for (TaskNode* node : all_nodes) {
            delete node;
        }
    }

    TaskNode* addTask(Task* task) {
        size_t delay = task->getFirstInterval();
        TaskNode* node = new TaskNode(task, delay);
        all_nodes.push_back(node);
        
        addTaskToWheels(node, delay);
        return node;
    }

    void cancelTask(TaskNode *p) {
        // Find and remove from appropriate wheel
        if (p->next != nullptr) {
            // Task is in a wheel, remove it
            if (isInWheel(&second_wheel, p)) {
                second_wheel.removeTaskNode(p);
            } else if (isInWheel(&minute_wheel, p)) {
                minute_wheel.removeTaskNode(p);
            } else if (isInWheel(&hour_wheel, p)) {
                hour_wheel.removeTaskNode(p);
            }
        }
        
        // Remove from all_nodes and delete
        for (size_t i = 0; i < all_nodes.size(); ++i) {
            if (all_nodes[i] == p) {
                all_nodes[i] = all_nodes[all_nodes.size() - 1];
                all_nodes.pop_back();
                break;
            }
        }
        delete p;
    }

    std::vector<Task*> tick() {
        std::vector<Task*> result;
        
        // Check if we need to cascade before advancing
        bool cascade_minute = (second_wheel.current_slot == 59);
        bool cascade_hour = cascade_minute && (minute_wheel.current_slot == 59);
        
        // Cascade from higher wheels if needed
        if (cascade_hour) {
            std::vector<TaskNode*> from_hour = hour_wheel.advance();
            for (TaskNode* node : from_hour) {
                node->time %= hour_wheel.interval;
                addTaskToWheels(node, node->time);
            }
        } else if (cascade_minute) {
            std::vector<TaskNode*> from_minute = minute_wheel.advance();
            for (TaskNode* node : from_minute) {
                node->time %= minute_wheel.interval;
                addTaskToWheels(node, node->time);
            }
        }
        
        // Advance second wheel
        std::vector<TaskNode*> ready = second_wheel.advance();
        
        // Execute ready tasks
        for (TaskNode* node : ready) {
            result.push_back(node->task);
            
            // Reschedule periodic tasks
            size_t period = node->task->getPeriod();
            if (period > 0) {
                node->time = period;
                addTaskToWheels(node, period);
            } else {
                // Non-periodic task, remove it
                for (size_t i = 0; i < all_nodes.size(); ++i) {
                    if (all_nodes[i] == node) {
                        all_nodes[i] = all_nodes[all_nodes.size() - 1];
                        all_nodes.pop_back();
                        break;
                    }
                }
                delete node;
            }
        }
        
        return result;
    }

private:
    TimingWheel second_wheel;
    TimingWheel minute_wheel;
    TimingWheel hour_wheel;
    std::vector<TaskNode*> all_nodes;
    
    void addTaskToWheels(TaskNode* node, size_t time) {
        // Try to add to second wheel
        if (time / second_wheel.interval <= second_wheel.size) {
            size_t slot_index = (second_wheel.current_slot + time / second_wheel.interval) % second_wheel.size;
            node->time = time;
            second_wheel.addTaskNode(node, slot_index);
        }
        // Try to add to minute wheel
        else if (time / minute_wheel.interval <= minute_wheel.size) {
            size_t slot_index = (minute_wheel.current_slot + time / minute_wheel.interval) % minute_wheel.size;
            node->time = time;
            minute_wheel.addTaskNode(node, slot_index);
        }
        // Try to add to hour wheel
        else if (time / hour_wheel.interval <= hour_wheel.size) {
            size_t slot_index = (hour_wheel.current_slot + time / hour_wheel.interval) % hour_wheel.size;
            node->time = time;
            hour_wheel.addTaskNode(node, slot_index);
        }
        // Task exceeds all wheels, delete it
        else {
            for (size_t i = 0; i < all_nodes.size(); ++i) {
                if (all_nodes[i] == node) {
                    all_nodes[i] = all_nodes[all_nodes.size() - 1];
                    all_nodes.pop_back();
                    break;
                }
            }
            delete node;
        }
    }
    
    bool isInWheel(TimingWheel* wheel, TaskNode* node) {
        for (size_t i = 0; i < wheel->size; ++i) {
            TaskNode* head = wheel->slots[i];
            if (head) {
                TaskNode* current = head;
                do {
                    if (current == node) {
                        return true;
                    }
                    current = current->next;
                } while (current != head);
            }
        }
        return false;
    }
};
