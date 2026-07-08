#pragma once

#include "ui/page.hpp"
#include "ui/render_context.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace torrview::ui {

class App {
public:
  App() = default;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  template <typename PageClass, typename Constructor>
  void register_page(Constructor&& constructor) {
    const std::type_index type = page_type<PageClass>();
    constructors_[type] = [constructor = std::forward<Constructor>(constructor)]() mutable {
      std::unique_ptr<PageClass> page = constructor();
      if (page == nullptr) {
        throw std::runtime_error("Page constructor returned null");
      }
      return std::unique_ptr<Page>(std::move(page));
    };
  }

  template <typename PageClass> PageClass& page() {
    const std::type_index type = page_type<PageClass>();
    auto existing = pages_.find(type);
    if (existing == pages_.end()) {
      existing = pages_.emplace(type, construct<PageClass>()).first;
    }
    return static_cast<PageClass&>(*existing->second);
  }

  template <typename PageClass> PageClass* find() {
    const auto existing = pages_.find(page_type<PageClass>());
    if (existing == pages_.end()) {
      return nullptr;
    }
    return static_cast<PageClass*>(existing->second.get());
  }

  template <typename PageClass> bool is_open() const {
    return active_type_ == page_type<PageClass>();
  }

  template <typename PageClass, typename... Args> PageClass& open(Args&&... args) {
    PageClass& next = page<PageClass>();
    if (active_page_ != nullptr && active_page_ != &next) {
      active_page_->close();
    }
    next.open(std::forward<Args>(args)...);
    active_page_ = &next;
    active_type_ = page_type<PageClass>();
    return next;
  }

  bool handle_event() {
    bool handled = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handle_event(event);
      handled = true;
    }
    return handled;
  }

  bool handle_event(const SDL_Event& event) {
    if (active_page_ != nullptr) {
      return active_page_->handle_event(event);
    }
    return false;
  }

  void draw(RenderContext& render_context) {
    if (active_page_ != nullptr) {
      active_page_->draw(render_context);
    }
  }

  void close() {
    if (active_page_ != nullptr) {
      active_page_->close();
      active_page_ = nullptr;
      active_type_ = std::type_index(typeid(void));
    }
  }

private:
  template <typename PageClass> static std::type_index page_type() {
    static_assert(std::is_base_of_v<Page, PageClass>, "PageClass must inherit ui::Page");
    return std::type_index(typeid(PageClass));
  }

  template <typename PageClass> std::unique_ptr<Page> construct() {
    const auto constructor = constructors_.find(page_type<PageClass>());
    if (constructor == constructors_.end()) {
      throw std::runtime_error("Page type is not registered");
    }
    return constructor->second();
  }

  std::unordered_map<std::type_index, std::function<std::unique_ptr<Page>()>> constructors_;
  std::unordered_map<std::type_index, std::unique_ptr<Page>> pages_;
  Page* active_page_ = nullptr;
  std::type_index active_type_ = std::type_index(typeid(void));
};

} // namespace torrview::ui
