#pragma once

#include <atomic>

namespace dxvk::sync {

  /**
   * \brief Lock-free single-linked list
   */
  template<typename T>
  class List {

    struct Entry {
      Entry(T&& data_)
      : data(std::move(data_)), next(nullptr) { }
      Entry(const T& data_)
      : data(data_), next(nullptr) { }
      template<typename... Args>
      Entry(Args... args)
      : data(std::forward<Args>(args)...), next(nullptr) { }

      T                   data;
      std::atomic<Entry*> next;
    };

  public:

    class Iterator {

    public:

      using iterator_category = std::forward_iterator_tag;
      using difference_type   = std::ptrdiff_t;
      using value_type        = T;
      using pointer           = T*;
      using reference         = T&;

      Iterator()
      : m_entry(nullptr) { }

      Iterator(Entry* e)
      : m_entry(e) { }

      reference operator * () const {
        return m_entry->data;
      }

      pointer operator -> () const {
        return &m_entry->data;
      }

      Iterator& operator ++ () {
        m_entry = m_entry->next;
        return *this;
      }

      Iterator operator ++ (int) {
        Iterator tmp(m_entry);
        m_entry = m_entry->next;
        return tmp;
      }

      bool operator == (const Iterator& other) const { return m_entry == other.m_entry; }
      bool operator != (const Iterator& other) const { return m_entry != other.m_entry; }

    private:

      Entry* m_entry;

    };

    using iterator = Iterator;

    List()
    : m_head(nullptr),
      m_tail(nullptr) { }
    List(List&& other)
    : m_head(other.m_head.load()),
      m_tail(other.m_tail.load()) {
      other.m_head = nullptr;
      other.m_tail = nullptr;
    }

    List& operator = (List&& other) {
      freeList(m_head.load());

      m_head = other.m_head.load();
      m_tail = other.m_tail.load();

      other.m_head = nullptr;
      other.m_tail = nullptr;
      return *this;
    }

    ~List() {
      freeList(m_head.load());
    }

    auto begin() const { return Iterator(m_head); }
    auto end() const { return Iterator(nullptr); }

    Iterator insert(const T& data) {
      return insertEntry(new Entry(data));
    }

    Iterator insert(T&& data) {
      return insertEntry(new Entry(std::move(data)));
    }

    template<typename... Args>
    Iterator emplace(Args... args) {
      return insertEntry(new Entry(std::forward<Args>(args)...));
    }

  private:

    std::atomic<Entry*> m_head;
    std::atomic<Entry*> m_tail;

    Iterator insertEntry(Entry* e) {
      Entry* next = m_tail.exchange(e, std::memory_order_acquire);

      auto atomic = next
        ? &next->next
        : &m_head;

      atomic->store(e, std::memory_order_release);
      return Iterator(e);
    }

    void freeList(Entry* e) {
      while (e) {
        Entry* next = e->next;;
        delete e;
        e = next;
      }
    }

  };

}
