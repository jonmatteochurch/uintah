#
#  The MIT License
#
#  Copyright (c) 1997-2020 The University of Utah
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to
#  deal in the Software without restriction, including without limitation the
#  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
#  sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
#  IN THE SOFTWARE.
#
#
#
#
#
# Makefile fragment for this subdirectory

SRCDIR := CCA/Components/PhaseField/BoundaryConditions

SRCS += \
  $(SRCDIR)/BCFDViewFactory-bld.cc \
  $(SRCDIR)/BCFDViewScalarProblemCCP5-bld.cc \
  $(SRCDIR)/BCFDViewScalarProblemCCP7-bld.cc \
  $(SRCDIR)/BCFDViewScalarProblemNCP5-bld.cc \
  $(SRCDIR)/BCFDViewScalarProblemNCP7-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemCCP5-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemCCP7-0-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemCCP7-1-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemCCP7-2-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemNCP5-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemNCP7-0-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemNCP7-1-bld.cc \
  $(SRCDIR)/BCFDViewHeatTestProblemNCP7-2-bld.cc \
  $(SRCDIR)/BCFDViewPureMetalProblemCCP5-bld.cc \
  $(SRCDIR)/BCFDViewPureMetalProblemNCP5-bld.cc \
  $(SRCDIR)/BCFDViewPureMetalProblemCCP7-bld.cc \

BLDDIR := $(SRCTOP)/$(SRCDIR)

BLDSRCS += \
  $(BLDDIR)/BCFDViewFactory-bld.cc \
  $(BLDDIR)/BCFDViewScalarProblemCCP5-bld.cc \
  $(BLDDIR)/BCFDViewScalarProblemCCP7-bld.cc \
  $(BLDDIR)/BCFDViewScalarProblemNCP5-bld.cc \
  $(BLDDIR)/BCFDViewScalarProblemNCP7-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemCCP5-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemCCP7-0-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemCCP7-1-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemCCP7-2-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemNCP5-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemNCP7-0-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemNCP7-1-bld.cc \
  $(BLDDIR)/BCFDViewHeatTestProblemNCP7-2-bld.cc \
  $(BLDDIR)/BCFDViewPureMetalProblemCCP5-bld.cc \
  $(BLDDIR)/BCFDViewPureMetalProblemNCP5-bld.cc \
  $(BLDDIR)/BCFDViewPureMetalProblemCCP7-bld.cc \
