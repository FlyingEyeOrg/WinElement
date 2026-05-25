#include <winelement/core.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace winelement::core;

TEST(CoreTests, LruCacheKeepsMostRecentValuesAndEvictsLeastRecent) {
    LruCache<int, std::string> cache(2U);

    cache.put(1, "one");
    cache.put(2, "two");
    ASSERT_NE(cache.get(1), nullptr);
    EXPECT_EQ(*cache.get(1), "one");

    cache.put(3, "three");

    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    ASSERT_NE(cache.get(3), nullptr);
    EXPECT_EQ(*cache.get(3), "three");
    EXPECT_EQ(cache.size(), 2U);
}

TEST(CoreTests, LruCacheCapacityResizeTrimsOldEntries) {
    LruCache<int, int> cache(4U);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);
    cache.put(4, 40);

    ASSERT_NE(cache.get(2), nullptr);
    cache.set_capacity(2U);

    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(4));
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.contains(3));
    EXPECT_EQ(cache.capacity(), 2U);
    EXPECT_EQ(cache.size(), 2U);
}

TEST(CoreTests, ObservableObjectNotifiesAndRemovesObservers) {
    auto model = std::make_shared<ObservableObject>();
    auto notifications = 0U;
    auto last_name = std::string{};
    const auto token = model->add_observer([&](const ObservableChange& change) {
        ++notifications;
        last_name = std::string{change.property_name};
    });

    model->set("title", std::string{"Ready"});
    EXPECT_EQ(notifications, 1U);
    EXPECT_EQ(last_name, "title");
    EXPECT_EQ(model->get<std::string>("title"), std::optional<std::string>{"Ready"});

    model->set("title", std::string{"Ready"});
    EXPECT_EQ(notifications, 1U);

    model->remove_observer(token);
    model->set("title", std::string{"Running"});
    EXPECT_EQ(notifications, 1U);
    EXPECT_EQ(model->get<std::string>("title"), std::optional<std::string>{"Running"});
}

TEST(CoreTests, ObservableObjectAllowsConcurrentReadHeavyAccess) {
    auto model = std::make_shared<ObservableObject>();
    model->set("counter", 0);
    std::atomic_bool start = false;
    std::atomic_int observed_reads = 0;

    auto readers = std::vector<std::thread>{};
    readers.reserve(4U);
    for (auto index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (auto iteration = 0; iteration < 1024; ++iteration) {
                if (model->get<int>("counter").has_value()) {
                    observed_reads.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        });
    }

    auto writer = std::thread([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (auto value = 1; value <= 512; ++value) {
            model->set("counter", value);
            EXPECT_GE(model->value_count(), 1U);
        }
    });

    start.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }
    writer.join();

    EXPECT_EQ(model->get<int>("counter"), std::optional<int>{512});
    EXPECT_GT(observed_reads.load(std::memory_order_acquire), 0);
}

TEST(CoreTests, EventHandlerSupportsConcurrentSubscriptionAndEmit) {
    EventHandler<int> event;
    std::atomic_int sum = 0;
    auto tokens = std::vector<EventToken>{};
    tokens.reserve(64U);

    for (auto index = 0; index < 64; ++index) {
        tokens.push_back(
            event.add([&sum](int value) { sum.fetch_add(value, std::memory_order_acq_rel); }));
    }

    auto emitters = std::vector<std::thread>{};
    emitters.reserve(4U);
    for (auto index = 0; index < 4; ++index) {
        emitters.emplace_back([&event] {
            for (auto iteration = 0; iteration < 100; ++iteration) {
                event.emit(1);
            }
        });
    }

    std::thread remover([&event, &tokens] {
        for (const auto token : tokens) {
            event.remove(token);
        }
    });

    for (auto& emitter : emitters) {
        emitter.join();
    }
    remover.join();

    const auto before_clear = sum.load(std::memory_order_acquire);
    event.clear();
    event.emit(1);
    EXPECT_EQ(sum.load(std::memory_order_acquire), before_clear);
    EXPECT_TRUE(event.empty());
}

TEST(CoreTests, ObservableListReportsRangeChanges) {
    ObservableStringList list;
    auto changes = std::vector<ObservableChange>{};
    list.add_observer([&](const ObservableChange& change) { changes.push_back(change); });

    list.append("A");
    list.insert(1U, "B");
    list.replace(0U, "C");
    list.erase(1U);

    ASSERT_EQ(changes.size(), 4U);
    EXPECT_EQ(changes[0].kind, ObservableChangeKind::Insert);
    EXPECT_EQ(changes[1].index, 1U);
    EXPECT_EQ(changes[2].kind, ObservableChangeKind::Replace);
    EXPECT_EQ(changes[3].kind, ObservableChangeKind::Remove);
    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list.at(0U), "C");
}

TEST(CoreTests, BindingExpressionEvaluatesNestedObjectListAndDefault) {
    auto first_item = std::make_shared<ObservableObject>();
    first_item->set("title", std::string{"Alpha"});
    auto items =
        std::make_shared<ObservableObjectList>(std::vector<ObservableObjectPtr>{first_item});
    auto model = std::make_shared<ObservableObject>();
    model->set("items", items);

    const auto expression = parse_binding_expression("items[0].title ?? 'Missing'");
    auto value = evaluate_binding_expression(*model, expression);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*coerce_property_value<std::string>(*value), "Alpha");

    const auto missing = parse_binding_expression("items[2].title ?? 'Missing'");
    value = evaluate_binding_expression(*model, missing);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*coerce_property_value<std::string>(*value), "Missing");

    EXPECT_TRUE(
        set_binding_expression_value(*model, expression, PropertyValue{std::string{"Beta"}}));
    EXPECT_EQ(first_item->get<std::string>("title"), std::optional<std::string>{"Beta"});
}

TEST(CoreTests, PropertyStoreOffersHumanReadableQueriesAndChangeFlags) {
    auto property = make_property<int>("demo.score", PropertyInvalidation::Layout |
                                                         PropertyInvalidation::Paint |
                                                         PropertyInvalidation::Semantics);
    PropertyStore store;

    EXPECT_TRUE(store.empty());
    EXPECT_FALSE(store.contains(property));
    EXPECT_EQ(store.try_value(property), std::nullopt);
    EXPECT_EQ(store.value_or(property, 7), 7);

    const auto change = store.set_value(property, 42);
    EXPECT_TRUE(change.changed);
    EXPECT_TRUE(change.requires_layout());
    EXPECT_TRUE(change.requires_paint());
    EXPECT_FALSE(change.requires_style());
    EXPECT_TRUE(change.requires_semantics());
    EXPECT_FALSE(change.is_inherited());
    EXPECT_FALSE(store.empty());
    EXPECT_TRUE(store.contains(property));
    EXPECT_EQ(store.try_value(property), std::optional<int>{42});
    EXPECT_EQ(store.value_or(property, 7), 42);

    const auto noop_change = store.set_value(property, 42);
    EXPECT_FALSE(noop_change.changed);
    EXPECT_FALSE(noop_change.requires_layout());
    EXPECT_FALSE(noop_change.requires_paint());
}

} // namespace
