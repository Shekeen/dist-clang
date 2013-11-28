#pragma once

#include <memory>

namespace dist_clang {
namespace base {

template <class T, class Default = T>
class Testable {
  public:
    class Factory {
      public:
        virtual ~Factory() {}

        virtual std::unique_ptr<T> Create() = 0;
    };

    class DefaultFactory: public Factory {
      public:
        virtual std::unique_ptr<T> Create() override {
          return std::unique_ptr<T>(new Default);
        }
    };

    static std::unique_ptr<T> Create();

    template <class F>
    static void SetFactory();

  private:
    static std::unique_ptr<Factory>& factory_();
};

template <class T>
class Testable<T, T> {
  public:
    class Factory {
      public:
        virtual ~Factory() {}

        virtual std::unique_ptr<T> Create() = 0;
    };

    static std::unique_ptr<T> Create();

    template <class F>
    static void SetFactory();

  private:
    static std::unique_ptr<Factory>& factory_();
};

// static
template <class T, class Default>
std::unique_ptr<T> Testable<T, Default>::Create() {
  if (!factory_()) {
    return typename std::unique_ptr<T>();
  }
  return factory_()->Create();
}

// static
template <class T, class Default>
template <class F>
void Testable<T, Default>::SetFactory() {
  factory_().reset(new F());
}

// static
template <class T, class D>
std::unique_ptr<typename Testable<T, D>::Factory>& Testable<T, D>::factory_() {
  static std::unique_ptr<Factory> factory(new DefaultFactory());
  return factory;
}

}  // namespace base
}  // namespace dist_clang