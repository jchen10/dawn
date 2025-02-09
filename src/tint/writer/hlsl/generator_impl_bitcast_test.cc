// Copyright 2020 The Tint Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/tint/utils/string_stream.h"
#include "src/tint/writer/hlsl/test_helper.h"

using namespace tint::number_suffixes;  // NOLINT

namespace tint::writer::hlsl {
namespace {

using HlslGeneratorImplTest_Bitcast = TestHelper;

TEST_F(HlslGeneratorImplTest_Bitcast, EmitExpression_Bitcast_Float) {
    auto* a = Let("a", Expr(1_i));
    auto* bitcast = Bitcast<f32>(Expr("a"));
    WrapInFunction(a, bitcast);

    GeneratorImpl& gen = Build();

    utils::StringStream out;
    ASSERT_TRUE(gen.EmitExpression(out, bitcast)) << gen.error();
    EXPECT_EQ(out.str(), "asfloat(a)");
}

TEST_F(HlslGeneratorImplTest_Bitcast, EmitExpression_Bitcast_Int) {
    auto* a = Let("a", Expr(1_u));
    auto* bitcast = Bitcast<i32>(Expr("a"));
    WrapInFunction(a, bitcast);

    GeneratorImpl& gen = Build();

    utils::StringStream out;
    ASSERT_TRUE(gen.EmitExpression(out, bitcast)) << gen.error();
    EXPECT_EQ(out.str(), "asint(a)");
}

TEST_F(HlslGeneratorImplTest_Bitcast, EmitExpression_Bitcast_Uint) {
    auto* a = Let("a", Expr(1_i));
    auto* bitcast = Bitcast<u32>(Expr("a"));
    WrapInFunction(a, bitcast);

    GeneratorImpl& gen = Build();

    utils::StringStream out;
    ASSERT_TRUE(gen.EmitExpression(out, bitcast)) << gen.error();
    EXPECT_EQ(out.str(), "asuint(a)");
}

}  // namespace
}  // namespace tint::writer::hlsl
