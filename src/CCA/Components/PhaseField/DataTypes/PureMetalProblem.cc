/*
 * The MIT License
 *
 * Copyright (c) 1997-2020 The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * @file CCA/Components/PhaseField/DataTypes/PureMetalProblem.cc
 * @author Jon Matteo Church [j.m.church@leeds.ac.uk]
 * @date 2018/12
 *
 * In this the names used by ApplicationFactory for the differnt implementations
 * of the PureMetal application are defined as well as their explicit instantiation.
 */

#include <CCA/Components/PhaseField/DataTypes/PureMetalProblem.h>

namespace Uintah
{
namespace PhaseField
{

/// @cond DOXYIGNORE
template<> const FactoryString PureMetalProblem<CC, P5>::Name = "PureMetalProblem";
template<> const FactoryString PureMetalProblem<NC, P5>::Name = "PureMetalProblem";
template<> const FactoryString PureMetalProblem<CC, P7>::Name = "PureMetalProblem";
template<> const FactoryString PureMetalProblem<NC, P7>::Name = "PureMetalProblem";
/// @endcond

} // namespace Uintah
} // namespace PhaseField
