// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include <WexTestClass.h>

#include "../cascadia/TerminalCore/Terminal.hpp"
#include "MockTermSettings.h"
#include "../renderer/inc/DummyRenderer.hpp"
#include "consoletaeftemplates.hpp"

using namespace winrt::Microsoft::Terminal::Core;
using namespace Microsoft::Terminal::Core;
using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;

namespace TerminalCoreUnitTests
{
#define WCS(x) WCSHELPER(x)
#define WCSHELPER(x) L## #x

    class ScreenSizeLimitsTest
    {
        TEST_CLASS(ScreenSizeLimitsTest);

        TEST_METHOD(ScreenWidthAndHeightAreClampedToBounds);
        TEST_METHOD(ScrollbackHistorySizeIsClampedToBounds);
        TEST_METHOD(InfiniteHistoryGrowsBeyondLegacyLimit);
        TEST_METHOD(InfiniteHistoryDoesNotGrowAlternateBuffer);
        TEST_METHOD(InfiniteHistoryPreservesScrolledViewport);
        TEST_METHOD(InfiniteHistoryClearShrinksAndRegrows);
        TEST_METHOD(InfiniteHistoryClearRebasesRetainedSelection);
        TEST_METHOD(InfiniteHistoryClearClipsSelectionAtCutoff);
        TEST_METHOD(InfiniteHistoryClearDropsDiscardedSelectionAndScrollOffset);

        TEST_METHOD(ResizeIsClampedToBounds);
        TEST_METHOD(InfiniteHistorySurvivesReflow);
        TEST_METHOD(InfiniteHistorySurvivesRepeatedMultiBlockReflow);
        TEST_METHOD(InfiniteHistoryHeightOnlyResizeKeepsBuffer);
        TEST_METHOD(InfiniteHistoryHeightOnlyResizeGrowsBuffer);
    };
}

using namespace TerminalCoreUnitTests;

void ScreenSizeLimitsTest::ScreenWidthAndHeightAreClampedToBounds()
{
    // Negative values for initial visible row count or column count
    // are clamped to 1. Too-large positive values are clamped to SHRT_MAX.
    auto negativeColumnsSettings = winrt::make<MockTermSettings>(10000, 9999999, -1234);
    Terminal negativeColumnsTerminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &negativeColumnsTerminal };
    negativeColumnsTerminal.CreateFromSettings(negativeColumnsSettings, renderer);
    auto actualDimensions = negativeColumnsTerminal.GetViewport().Dimensions();
    VERIFY_ARE_EQUAL(actualDimensions.height, SHRT_MAX, L"Row count clamped to SHRT_MAX == " WCS(SHRT_MAX));
    VERIFY_ARE_EQUAL(actualDimensions.width, 1, L"Column count clamped to 1");

    // Zero values are clamped to 1 as well.
    auto zeroRowsSettings = winrt::make<MockTermSettings>(10000, 0, 9999999);
    Terminal zeroRowsTerminal{ Terminal::TestDummyMarker{} };
    zeroRowsTerminal.CreateFromSettings(zeroRowsSettings, renderer);
    actualDimensions = zeroRowsTerminal.GetViewport().Dimensions();
    VERIFY_ARE_EQUAL(actualDimensions.height, 1, L"Row count clamped to 1");
    VERIFY_ARE_EQUAL(actualDimensions.width, SHRT_MAX, L"Column count clamped to SHRT_MAX == " WCS(SHRT_MAX));
}

void ScreenSizeLimitsTest::ScrollbackHistorySizeIsClampedToBounds()
{
    // What is actually clamped is the number of rows in the internal history buffer,
    // which is the *sum* of the history size plus the number of rows
    // actually visible on screen at the moment.

    static constexpr til::CoordType visibleRowCount = 100;

    // Zero history size is acceptable.
    auto noHistorySettings = winrt::make<MockTermSettings>(0, visibleRowCount, 100);
    Terminal noHistoryTerminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &noHistoryTerminal };
    noHistoryTerminal.CreateFromSettings(noHistorySettings, renderer);
    VERIFY_ARE_EQUAL(noHistoryTerminal.GetTextBuffer().TotalRowCount(), visibleRowCount, L"History size of 0 is accepted");

    // Negative history sizes other than the -1 sentinel are clamped to zero.
    auto negativeHistorySizeSettings = winrt::make<MockTermSettings>(-100, visibleRowCount, 100);
    Terminal negativeHistorySizeTerminal{ Terminal::TestDummyMarker{} };
    negativeHistorySizeTerminal.CreateFromSettings(negativeHistorySizeSettings, renderer);
    VERIFY_ARE_EQUAL(negativeHistorySizeTerminal.GetTextBuffer().TotalRowCount(), visibleRowCount, L"Negative history size is clamped to 0");

    // History size + initial visible rows == SHRT_MAX is acceptable.
    auto maxHistorySizeSettings = winrt::make<MockTermSettings>(SHRT_MAX - visibleRowCount, visibleRowCount, 100);
    Terminal maxHistorySizeTerminal{ Terminal::TestDummyMarker{} };
    maxHistorySizeTerminal.CreateFromSettings(maxHistorySizeSettings, renderer);
    VERIFY_ARE_EQUAL(maxHistorySizeTerminal.GetTextBuffer().TotalRowCount(), SHRT_MAX, L"History size == SHRT_MAX - initial row count is accepted");

    // History size + initial visible rows == SHRT_MAX + 1 will be clamped slightly.
    auto justTooBigHistorySizeSettings = winrt::make<MockTermSettings>(SHRT_MAX - visibleRowCount + 1, visibleRowCount, 100);
    Terminal justTooBigHistorySizeTerminal{ Terminal::TestDummyMarker{} };
    justTooBigHistorySizeTerminal.CreateFromSettings(justTooBigHistorySizeSettings, renderer);
    VERIFY_ARE_EQUAL(justTooBigHistorySizeTerminal.GetTextBuffer().TotalRowCount(), SHRT_MAX, L"History size == 1 + SHRT_MAX - initial row count is clamped to SHRT_MAX - initial row count");

    // Ridiculously large history sizes are also clamped.
    auto farTooBigHistorySizeSettings = winrt::make<MockTermSettings>(99999999, visibleRowCount, 100);
    Terminal farTooBigHistorySizeTerminal{ Terminal::TestDummyMarker{} };
    farTooBigHistorySizeTerminal.CreateFromSettings(farTooBigHistorySizeSettings, renderer);
    VERIFY_ARE_EQUAL(farTooBigHistorySizeTerminal.GetTextBuffer().TotalRowCount(), SHRT_MAX, L"History size that is far too large is clamped to SHRT_MAX - initial row count");
}

void ScreenSizeLimitsTest::InfiniteHistoryGrowsBeyondLegacyLimit()
{
    static constexpr til::CoordType visibleRowCount = 4;
    auto settings = winrt::make<MockTermSettings>(-1, visibleRowCount, 8);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    std::wstring output{ L"first\r\n" };
    for (til::CoordType i = 0; i < SHRT_MAX + visibleRowCount; ++i)
    {
        output.append(L"x\r\n");
    }
    terminal.Write(output);

    const auto& textBuffer = terminal.GetTextBuffer();
    VERIFY_IS_TRUE(textBuffer.TotalRowCount() > SHRT_MAX);
    VERIFY_ARE_EQUAL(std::wstring_view{ L"first" }, textBuffer.GetRowByOffset(0).GetText().substr(0, 5));
    VERIFY_ARE_EQUAL(0, textBuffer.GetFirstRowIndex(), L"Unlimited history must append instead of rotating");
}

void ScreenSizeLimitsTest::InfiniteHistoryDoesNotGrowAlternateBuffer()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 8);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"main\r\nmain\r\nmain\r\nmain\r\nmain\r\n");
    const auto mainBufferHeight = terminal.GetTextBuffer().TotalRowCount();
    VERIFY_IS_TRUE(mainBufferHeight > 4);

    terminal.Write(L"\x1b[?1049h");
    VERIFY_IS_FALSE(terminal.GetTextBuffer().IsGrowable());
    for (auto i = 0; i < 100; ++i)
    {
        terminal.Write(L"alt\r\n");
    }
    VERIFY_ARE_EQUAL(4, terminal.GetTextBuffer().TotalRowCount());

    terminal.Write(L"\x1b[?1049l");
    VERIFY_IS_TRUE(terminal.GetTextBuffer().IsGrowable());
    VERIFY_ARE_EQUAL(mainBufferHeight, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(std::wstring_view{ L"main" }, terminal.GetTextBuffer().GetRowByOffset(0).GetText().substr(0, 4));
}

void ScreenSizeLimitsTest::InfiniteHistoryPreservesScrolledViewport()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 8);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"anchor\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n");
    terminal.UserScrollViewport(0);
    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());

    terminal.Write(L"8\r\n9\r\n10\r\n11\r\n12\r\n");

    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(std::wstring_view{ L"anchor" }, terminal.GetTextBuffer().GetRowByOffset(0).GetText().substr(0, 6));
}

void ScreenSizeLimitsTest::InfiniteHistoryClearShrinksAndRegrows()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 8);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"0\r\n1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n");
    VERIFY_IS_TRUE(terminal.GetTextBuffer().TotalRowCount() > 4);

    terminal.Write(L"\x1b[3J");
    VERIFY_ARE_EQUAL(4, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(0, terminal.GetTextBuffer().GetFirstRowIndex());
    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());

    terminal.Write(L"8\r\n9\r\n10\r\n11\r\n12\r\n");
    VERIFY_IS_TRUE(terminal.GetTextBuffer().TotalRowCount() > 4);
}

void ScreenSizeLimitsTest::InfiniteHistoryClearRebasesRetainedSelection()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 16);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\n");
    const auto rowsRemoved = terminal.ViewStartIndex();
    VERIFY_IS_TRUE(rowsRemoved > 0);

    // Select text in the live viewport. ED3 retains this row, but moves it to
    // the beginning of the compacted buffer.
    terminal.SetSelectionAnchor({ 0, 0 });
    terminal.SetSelectionEnd({ 5, 0 });
    const auto selectedBefore = terminal.RetrieveSelectedTextFromBuffer(false).plainText;
    VERIFY_ARE_EQUAL(std::wstring{ L"line5" }, selectedBefore);
    VERIFY_ARE_EQUAL(rowsRemoved, terminal.GetSelectionAnchor().y);

    terminal.Write(L"\x1b[3J");

    VERIFY_IS_TRUE(terminal.IsSelectionActive());
    VERIFY_ARE_EQUAL(0, terminal.GetSelectionAnchor().y);
    VERIFY_ARE_EQUAL(0, terminal.GetSelectionEnd().y);
    VERIFY_ARE_EQUAL(selectedBefore, terminal.RetrieveSelectedTextFromBuffer(false).plainText);
    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());

    // Selection keeps the retained row visible while new output grows the
    // buffer again.
    terminal.Write(L"next\r\n");
    VERIFY_IS_TRUE(terminal.ViewStartIndex() > 0);
    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(selectedBefore, terminal.RetrieveSelectedTextFromBuffer(false).plainText);
}

void ScreenSizeLimitsTest::InfiniteHistoryClearClipsSelectionAtCutoff()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 16);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\n");
    const auto rowsRemoved = terminal.ViewStartIndex();
    VERIFY_IS_TRUE(rowsRemoved > 0);

    // Begin in the final discarded row and end in the first retained row.
    terminal.UserScrollViewport(rowsRemoved - 1);
    terminal.SetSelectionAnchor({ 2, 0 });
    terminal.SetSelectionEnd({ 3, 1 });

    terminal.Write(L"\x1b[3J");

    VERIFY_IS_TRUE(terminal.IsSelectionActive());
    VERIFY_ARE_EQUAL((til::point{}), terminal.GetSelectionAnchor());
    VERIFY_ARE_EQUAL((til::point{ 3, 0 }), terminal.GetSelectionEnd());
    VERIFY_ARE_EQUAL(std::wstring{ L"lin" }, terminal.RetrieveSelectedTextFromBuffer(false).plainText);
}

void ScreenSizeLimitsTest::InfiniteHistoryClearDropsDiscardedSelectionAndScrollOffset()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 16);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"line0\r\nline1\r\nline2\r\nline3\r\nline4\r\nline5\r\nline6\r\nline7\r\n");
    const auto rowsRemoved = terminal.ViewStartIndex();
    VERIFY_IS_TRUE(rowsRemoved > 0);

    // Select a half-open range whose end is exactly the first retained cell.
    // ED3 discards the entire range.
    terminal.UserScrollViewport(rowsRemoved - 1);
    terminal.SetSelectionAnchor({ 0, 0 });
    terminal.SetSelectionEnd({ 0, 1 });
    VERIFY_ARE_EQUAL((til::point{ 0, rowsRemoved }), terminal.GetSelectionEnd());
    terminal.ToggleMarkMode();
    VERIFY_IS_TRUE(Terminal::SelectionInteractionMode::Mark == terminal.SelectionMode());

    terminal.Write(L"\x1b[3J");

    VERIFY_IS_FALSE(terminal.IsSelectionActive());
    VERIFY_IS_TRUE(Terminal::SelectionInteractionMode::None == terminal.SelectionMode());
    VERIFY_IS_TRUE(terminal.RetrieveSelectedTextFromBuffer(false).plainText.empty());
    VERIFY_ARE_EQUAL(0, terminal.GetViewport().Top());

    // Entering mark mode again must create a fresh selection instead of
    // interpreting the stale pre-compaction mode as a request to leave it.
    terminal.ToggleMarkMode();
    VERIFY_IS_TRUE(terminal.IsSelectionActive());
    VERIFY_IS_TRUE(Terminal::SelectionInteractionMode::Mark == terminal.SelectionMode());
    VERIFY_IS_TRUE(terminal.GetSelectionAnchor().y < terminal.GetTextBuffer().TotalRowCount());
    terminal.ClearSelection();

    // The old user scroll offset referred only to discarded history. Once the
    // selection is gone, subsequent output should follow the mutable viewport.
    terminal.Write(L"after\r\n");
    VERIFY_IS_TRUE(terminal.ViewStartIndex() > 0);
    VERIFY_ARE_EQUAL(terminal.ViewStartIndex(), terminal.GetViewport().Top());
}

void ScreenSizeLimitsTest::ResizeIsClampedToBounds()
{
    // What is actually clamped is the number of rows in the internal history buffer,
    // which is the *sum* of the history size plus the number of rows
    // actually visible on screen at the moment.
    //
    // This is a test for GH#2630, GH#2815.

    static constexpr til::CoordType initialVisibleColCount = 50;
    static constexpr til::CoordType initialVisibleRowCount = 50;
    const auto historySize = SHRT_MAX - (initialVisibleRowCount * 2);

    Log::Comment(L"Watch out - this test takes a while on debug, because "
                 L"ResizeWithReflow takes a while on debug. This is expected.");

    auto settings = winrt::make<MockTermSettings>(historySize, initialVisibleRowCount, initialVisibleColCount);
    Log::Comment(L"First create a terminal with fewer than SHRT_MAX lines");
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);
    VERIFY_ARE_EQUAL(terminal.GetTextBuffer().TotalRowCount(), historySize + initialVisibleRowCount);

    Log::Comment(L"Resize the terminal to have exactly SHRT_MAX lines");
    VERIFY_SUCCEEDED(terminal.UserResize({ initialVisibleColCount, initialVisibleRowCount * 2 }));

    VERIFY_ARE_EQUAL(terminal.GetTextBuffer().TotalRowCount(), SHRT_MAX);

    Log::Comment(L"Resize the terminal to have MORE than SHRT_MAX lines - we should clamp to SHRT_MAX");
    VERIFY_SUCCEEDED(terminal.UserResize({ initialVisibleColCount, initialVisibleRowCount * 3 }));
    VERIFY_ARE_EQUAL(terminal.GetTextBuffer().TotalRowCount(), SHRT_MAX);

    Log::Comment(L"Resize back down to the original size");
    VERIFY_SUCCEEDED(terminal.UserResize({ initialVisibleColCount, initialVisibleRowCount }));
    VERIFY_ARE_EQUAL(terminal.GetTextBuffer().TotalRowCount(), historySize + initialVisibleRowCount);
}

void ScreenSizeLimitsTest::InfiniteHistorySurvivesReflow()
{
    auto settings = winrt::make<MockTermSettings>(-1, 4, 16);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    terminal.Write(L"first-line\r\n");
    for (auto i = 0; i < 100; ++i)
    {
        terminal.Write(L"history-line\r\n");
    }

    const auto heightBeforeResize = terminal.GetTextBuffer().TotalRowCount();
    VERIFY_SUCCEEDED(terminal.UserResize({ 8, 4 }));

    const auto& textBuffer = terminal.GetTextBuffer();
    VERIFY_IS_TRUE(textBuffer.TotalRowCount() > heightBeforeResize);
    VERIFY_ARE_EQUAL(std::wstring_view{ L"first-li" }, textBuffer.GetRowByOffset(0).GetText().substr(0, 8));
    VERIFY_ARE_EQUAL(0, textBuffer.GetFirstRowIndex());

    const auto heightAfterResize = textBuffer.TotalRowCount();
    terminal.Write(L"after-resize\r\n");
    VERIFY_IS_TRUE(terminal.GetTextBuffer().TotalRowCount() > heightAfterResize);
}

void ScreenSizeLimitsTest::InfiniteHistorySurvivesRepeatedMultiBlockReflow()
{
    static constexpr til::CoordType viewportHeight = 4;
    static constexpr til::CoordType initialWidth = 32;
    static constexpr std::wstring_view firstAnchor{ L"first-anchor" };
    static constexpr std::wstring_view tailAnchor{ L"tailmark" };
    auto settings = winrt::make<MockTermSettings>(-1, viewportHeight, initialWidth);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    // More than 4096 rows guarantees that the growable buffer spans multiple
    // storage blocks. The line width also forces wrapping at the narrow sizes.
    std::wstring output{ firstAnchor };
    output.append(L"\r\n");
    for (auto i = 0; i < 5000; ++i)
    {
        output.append(L"history-line\r\n");
    }
    output.append(tailAnchor);
    output.append(L"\r\n");
    terminal.Write(output);

    const auto verifyAnchorsAndBounds = [&]() {
        const auto& textBuffer = terminal.GetTextBuffer();
        VERIFY_IS_TRUE(textBuffer.IsGrowable());
        VERIFY_ARE_EQUAL(0, textBuffer.GetFirstRowIndex());
        VERIFY_IS_TRUE(textBuffer.TotalRowCount() > 5000);

        std::wstring actualFirstAnchor;
        for (til::CoordType y = 0; y < textBuffer.TotalRowCount() && actualFirstAnchor.size() < firstAnchor.size(); ++y)
        {
            const auto& row = textBuffer.GetRowByOffset(y);
            const auto rowText = row.GetText();
            const auto remaining = firstAnchor.size() - actualFirstAnchor.size();
            actualFirstAnchor.append(rowText.substr(0, std::min(rowText.size(), remaining)));
            if (!row.WasWrapForced())
            {
                break;
            }
        }
        VERIFY_ARE_EQUAL(std::wstring{ firstAnchor }, actualFirstAnchor, L"The first logical line must survive every reflow");

        auto foundTailAnchor = false;
        const auto firstCandidate = std::max<til::CoordType>(0, textBuffer.TotalRowCount() - 16);
        for (auto y = firstCandidate; y < textBuffer.TotalRowCount(); ++y)
        {
            const auto rowText = textBuffer.GetRowByOffset(y).GetText();
            if (rowText.size() >= tailAnchor.size() && rowText.substr(0, tailAnchor.size()) == tailAnchor)
            {
                foundTailAnchor = true;
                break;
            }
        }
        VERIFY_IS_TRUE(foundTailAnchor, L"The tail of the retained history must survive every reflow");

        const auto cursor = textBuffer.GetCursor().GetPosition();
        VERIFY_IS_TRUE(cursor.x >= 0 && cursor.x < textBuffer.GetSize().Width());
        VERIFY_IS_TRUE(cursor.y >= 0 && cursor.y < textBuffer.TotalRowCount());
    };

    verifyAnchorsAndBounds();

    static constexpr til::CoordType resizeWidths[]{ 10, 24, 14, 40, 16, initialWidth };
    for (const auto width : resizeWidths)
    {
        VERIFY_SUCCEEDED(terminal.UserResize({ width, viewportHeight }));
        VERIFY_ARE_EQUAL(width, terminal.GetViewport().Width());
        verifyAnchorsAndBounds();
    }

    // Exercise the growth path again after the final reflow. More writes than
    // the viewport height guarantee that at least one new row must be appended.
    const auto heightAfterReflows = terminal.GetTextBuffer().TotalRowCount();
    terminal.Write(L"post-reflow-0\r\npost-reflow-1\r\npost-reflow-2\r\npost-reflow-3\r\n"
                   L"post-reflow-4\r\npost-reflow-5\r\npost-reflow-6\r\npost-reflow-7\r\n");
    VERIFY_IS_TRUE(terminal.GetTextBuffer().TotalRowCount() > heightAfterReflows);
    verifyAnchorsAndBounds();
}

void ScreenSizeLimitsTest::InfiniteHistoryHeightOnlyResizeKeepsBuffer()
{
    static constexpr til::CoordType initialHeight = 4;
    static constexpr til::CoordType width = 16;
    auto settings = winrt::make<MockTermSettings>(-1, initialHeight, width);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    // More than 4096 rows guarantees that growable storage spans multiple
    // blocks, regardless of the row width used by TextBuffer.
    std::wstring output{ L"anchor\r\n" };
    for (auto i = 0; i < 5000; ++i)
    {
        output.append(L"history-line\r\n");
    }
    terminal.Write(output);

    terminal.UserScrollViewport(100);
    const auto* const bufferBeforeResize = &terminal.GetTextBuffer();
    const auto* const firstRowBeforeResize = &bufferBeforeResize->GetRowByOffset(0);
    const auto* const laterRowBeforeResize = &bufferBeforeResize->GetRowByOffset(4097);
    const auto heightBeforeResize = bufferBeforeResize->TotalRowCount();
    const auto cursorBeforeResize = bufferBeforeResize->GetCursor().GetPosition();
    const auto visibleTopBeforeResize = terminal.GetViewport().Top();

    terminal.SetSelectionAnchor({ 0, 0 });
    terminal.SetSelectionEnd({ 7, 0 });
    const auto selectionAnchorBeforeResize = terminal.GetSelectionAnchor();
    const auto selectionEndBeforeResize = terminal.GetSelectionEnd();
    const auto selectedTextBeforeResize = terminal.RetrieveSelectedTextFromBuffer(false).plainText;

    VERIFY_SUCCEEDED(terminal.UserResize({ width, 8 }));
    VERIFY_IS_TRUE(bufferBeforeResize == &terminal.GetTextBuffer(), L"Height-only resize must retain the existing buffer");
    VERIFY_IS_TRUE(firstRowBeforeResize == &terminal.GetTextBuffer().GetRowByOffset(0));
    VERIFY_IS_TRUE(laterRowBeforeResize == &terminal.GetTextBuffer().GetRowByOffset(4097));
    VERIFY_ARE_EQUAL(heightBeforeResize, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(cursorBeforeResize, terminal.GetTextBuffer().GetCursor().GetPosition());
    VERIFY_ARE_EQUAL(visibleTopBeforeResize, terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(selectionAnchorBeforeResize, terminal.GetSelectionAnchor());
    VERIFY_ARE_EQUAL(selectionEndBeforeResize, terminal.GetSelectionEnd());
    VERIFY_ARE_EQUAL(selectedTextBeforeResize, terminal.RetrieveSelectedTextFromBuffer(false).plainText);
    VERIFY_ARE_EQUAL(8, terminal.GetViewport().Height());
    VERIFY_ARE_EQUAL(std::wstring_view{ L"anchor" }, terminal.GetTextBuffer().GetRowByOffset(0).GetText().substr(0, 6));

    VERIFY_SUCCEEDED(terminal.UserResize({ width, 2 }));
    VERIFY_IS_TRUE(bufferBeforeResize == &terminal.GetTextBuffer(), L"Repeated height-only resize must retain the existing buffer");
    VERIFY_IS_TRUE(firstRowBeforeResize == &terminal.GetTextBuffer().GetRowByOffset(0));
    VERIFY_IS_TRUE(laterRowBeforeResize == &terminal.GetTextBuffer().GetRowByOffset(4097));
    VERIFY_ARE_EQUAL(heightBeforeResize, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(cursorBeforeResize, terminal.GetTextBuffer().GetCursor().GetPosition());
    VERIFY_ARE_EQUAL(visibleTopBeforeResize, terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(selectionAnchorBeforeResize, terminal.GetSelectionAnchor());
    VERIFY_ARE_EQUAL(selectionEndBeforeResize, terminal.GetSelectionEnd());
    VERIFY_ARE_EQUAL(selectedTextBeforeResize, terminal.RetrieveSelectedTextFromBuffer(false).plainText);
    VERIFY_ARE_EQUAL(2, terminal.GetViewport().Height());
    VERIFY_ARE_EQUAL(std::wstring_view{ L"anchor" }, terminal.GetTextBuffer().GetRowByOffset(0).GetText().substr(0, 6));

    // Return to the live viewport and verify that a height-only grow followed
    // by a shrink continues following the bottom of this multi-block history.
    terminal.ClearSelection();
    terminal.UserScrollViewport(terminal.ViewStartIndex());
    VERIFY_ARE_EQUAL(terminal.ViewStartIndex(), terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(cursorBeforeResize.y, terminal.GetViewport().BottomInclusive());

    VERIFY_SUCCEEDED(terminal.UserResize({ width, 7 }));
    VERIFY_IS_TRUE(bufferBeforeResize == &terminal.GetTextBuffer());
    VERIFY_ARE_EQUAL(heightBeforeResize, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(cursorBeforeResize, terminal.GetTextBuffer().GetCursor().GetPosition());
    VERIFY_ARE_EQUAL(7, terminal.GetViewport().Height());
    VERIFY_ARE_EQUAL(terminal.ViewStartIndex(), terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(cursorBeforeResize.y, terminal.GetViewport().BottomInclusive());

    VERIFY_SUCCEEDED(terminal.UserResize({ width, 3 }));
    VERIFY_IS_TRUE(bufferBeforeResize == &terminal.GetTextBuffer());
    VERIFY_ARE_EQUAL(heightBeforeResize, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(cursorBeforeResize, terminal.GetTextBuffer().GetCursor().GetPosition());
    VERIFY_ARE_EQUAL(3, terminal.GetViewport().Height());
    VERIFY_ARE_EQUAL(terminal.ViewStartIndex(), terminal.GetViewport().Top());
    VERIFY_ARE_EQUAL(cursorBeforeResize.y, terminal.GetViewport().BottomInclusive());
}

void ScreenSizeLimitsTest::InfiniteHistoryHeightOnlyResizeGrowsBuffer()
{
    static constexpr til::CoordType initialHeight = 4;
    static constexpr til::CoordType enlargedHeight = 5000;
    static constexpr til::CoordType width = 16;
    auto settings = winrt::make<MockTermSettings>(-1, initialHeight, width);
    Terminal terminal{ Terminal::TestDummyMarker{} };
    DummyRenderer renderer{ &terminal };
    terminal.CreateFromSettings(settings, renderer);

    const auto* const bufferBeforeResize = &terminal.GetTextBuffer();
    const auto* const firstRowBeforeResize = &bufferBeforeResize->GetRowByOffset(0);
    const auto cursorBeforeResize = bufferBeforeResize->GetCursor().GetPosition();

    // This crosses the maximum row-block size, exercising in-place storage
    // growth as well as the viewport-only portion of the fast path.
    VERIFY_SUCCEEDED(terminal.UserResize({ width, enlargedHeight }));
    VERIFY_IS_TRUE(bufferBeforeResize == &terminal.GetTextBuffer());
    VERIFY_IS_TRUE(firstRowBeforeResize == &terminal.GetTextBuffer().GetRowByOffset(0));
    VERIFY_ARE_EQUAL(enlargedHeight, terminal.GetTextBuffer().TotalRowCount());
    VERIFY_ARE_EQUAL(cursorBeforeResize, terminal.GetTextBuffer().GetCursor().GetPosition());
    VERIFY_ARE_EQUAL((til::point{}), terminal.GetViewport().Origin());
    VERIFY_ARE_EQUAL(enlargedHeight, terminal.GetViewport().Height());

    terminal.Write(L"after-resize\r\n");
    VERIFY_ARE_EQUAL(std::wstring_view{ L"after-resize" }, terminal.GetTextBuffer().GetRowByOffset(0).GetText().substr(0, 12));
}
