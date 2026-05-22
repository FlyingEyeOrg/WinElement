#include <winelement/core.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
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

} // namespace
