// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/serialize_host_descriptions.h"

#include <utility>
#include <vector>

#include "base/values.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::UnorderedElementsAre;

namespace {

HostDescriptionNode GetNodeWithLabel(const char* name, int label) {
  HostDescriptionNode node = {name, std::string(),
                              base::Value(base::Value::Type::DICTIONARY)};
  node.representation.SetIntKey("label", label);
  return node;
}

// Returns the list of children of |arg|.
const base::Value* GetChildren(const base::Value& arg) {
  const base::DictionaryValue* dict = nullptr;
  EXPECT_TRUE(arg.GetAsDictionary(&dict));

  const base::Value* children = nullptr;
  if (!dict->Get("children", &children))
    return nullptr;
  EXPECT_EQ(base::Value::Type::LIST, children->type());
  return children;
}

// Checks that |arg| is a description of a node with label |l|.
bool CheckLabel(const base::Value& arg, int l) {
  const base::DictionaryValue* dict = nullptr;
  EXPECT_TRUE(arg.GetAsDictionary(&dict));
  absl::optional<int> result = dict->FindIntKey("label");
  if (!result)
    return false;
  return l == *result;
}

// Matches every |arg| with label |label| and checks that it has no children.
MATCHER_P(EmptyNode, label, "") {
  if (!CheckLabel(arg, label))
    return false;
  EXPECT_FALSE(GetChildren(arg));
  return true;
}

}  // namespace

TEST(SerializeHostDescriptionTest, Empty) {
  std::vector<base::Value> result =
      SerializeHostDescriptions(std::vector<HostDescriptionNode>(), "123");
  EXPECT_THAT(result, ::testing::IsEmpty());
}

// Test serializing a forest of stubs (no edges).
TEST(SerializeHostDescriptionTest, Stubs) {
  std::vector<HostDescriptionNode> nodes;
  nodes.emplace_back(GetNodeWithLabel("1", 1));
  nodes.emplace_back(GetNodeWithLabel("2", 2));
  nodes.emplace_back(GetNodeWithLabel("3", 3));
  std::vector<base::Value> result =
      SerializeHostDescriptions(std::move(nodes), "children");
  EXPECT_THAT(result,
              UnorderedElementsAre(EmptyNode(1), EmptyNode(2), EmptyNode(3)));
}

// Test handling multiple nodes sharing the same name.
TEST(SerializeHostDescriptionTest, SameNames) {
  std::vector<HostDescriptionNode> nodes;
  nodes.emplace_back(GetNodeWithLabel("A", 1));
  nodes.emplace_back(GetNodeWithLabel("A", 2));
  nodes.emplace_back(GetNodeWithLabel("A", 3));
  nodes.emplace_back(GetNodeWithLabel("B", 4));
  nodes.emplace_back(GetNodeWithLabel("C", 5));

  std::vector<base::Value> result =
      SerializeHostDescriptions(std::move(nodes), "children");

  // Only the first node called "A", and both nodes "B" and "C" should be
  // returned.
  EXPECT_THAT(result,
              UnorderedElementsAre(EmptyNode(1), EmptyNode(4), EmptyNode(5)));
}

// Test serializing a small forest, of this structure:
// 5 -- 2 -- 4
// 0 -- 6
//   \ 1
//   \ 3

namespace {

// Matchers for non-empty nodes specifically in this test:
MATCHER(Node2, "") {
  if (!CheckLabel(arg, 2))
    return false;
  EXPECT_THAT(GetChildren(arg)->GetList(), UnorderedElementsAre(EmptyNode(4)));
  return true;
}

MATCHER(Node5, "") {
  if (!CheckLabel(arg, 5))
    return false;
  EXPECT_THAT(GetChildren(arg)->GetList(), UnorderedElementsAre(Node2()));
  return true;
}

MATCHER(Node0, "") {
  if (!CheckLabel(arg, 0))
    return false;
  EXPECT_THAT(GetChildren(arg)->GetList(),
              UnorderedElementsAre(EmptyNode(1), EmptyNode(3), EmptyNode(6)));
  return true;
}

}  // namespace

TEST(SerializeHostDescriptionTest, Forest) {
  std::vector<HostDescriptionNode> nodes(7);
  const char* kNames[] = {"0", "1", "2", "3", "4", "5", "6"};
  for (size_t i = 0; i < 7; ++i) {
    nodes[i] = GetNodeWithLabel(kNames[i], i);
  }
  nodes[2].parent_name = "5";
  nodes[4].parent_name = "2";
  nodes[6].parent_name = "0";
  nodes[1].parent_name = "0";
  nodes[3].parent_name = "0";

  std::vector<base::Value> result =
      SerializeHostDescriptions(std::move(nodes), "children");

  EXPECT_THAT(result, UnorderedElementsAre(Node0(), Node5()));
}
