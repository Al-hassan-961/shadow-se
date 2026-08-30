// SPDX-License-Identifier: MIT
// Shadow SE - enhanced HTML extraction tests (scripts/styles/boilerplate).
#include "shadowse/crawler.hpp"

#include "test_framework.hpp"

#include <string>

using shadowse::htmlExtractTitle;
using shadowse::htmlStripTags;

TEST(html_strips_scripts_and_styles) {
    const std::string html =
        "<html><head><title>Real Title</title></head><body>"
        "<script>var x = a < b; if (x) { alert('hi'); }</script>"
        "<style>.css { color: red; }</style>"
        "<p>Visible paragraph.</p>"
        "</body></html>";
    const std::string text = htmlStripTags(html);
    CHECK(text.find("alert") == std::string::npos);
    CHECK(text.find("color: red") == std::string::npos);
    CHECK(text.find("var x") == std::string::npos);
    CHECK(text.find("Visible paragraph.") != std::string::npos);
    CHECK_EQ(htmlExtractTitle(html), "Real Title");
}

TEST(html_strips_comments) {
    const std::string html = "Start <!-- hidden junk --> End";
    CHECK_EQ(htmlStripTags(html), "Start End");
}

TEST(html_strips_boilerplate_blocks) {
    const std::string html =
        "<nav>Home About Contact</nav>"
        "<header>Site Header Logo</header>"
        "<aside>Sidebar ads</aside>"
        "<footer>&copy; 2026 All rights reserved</footer>"
        "<main>Actual article body here.</main>";
    const std::string text = htmlStripTags(html);
    CHECK(text.find("Home About Contact") == std::string::npos);
    CHECK(text.find("Site Header Logo") == std::string::npos);
    CHECK(text.find("Sidebar ads") == std::string::npos);
    CHECK(text.find("All rights reserved") == std::string::npos);
    CHECK(text.find("Actual article body here.") != std::string::npos);
}

TEST(html_unterminated_script_does_not_emit_text) {
    const std::string html = "<p>before</p><script>never closed";
    const std::string text = htmlStripTags(html);
    CHECK(text.find("never closed") == std::string::npos);
    CHECK(text.find("before") != std::string::npos);
}

TEST(html_self_closing_svg_is_not_a_block) {
    const std::string html = "<p>a</p><svg width=\"10\"/><p>b</p>";
    const std::string text = htmlStripTags(html);
    CHECK(text.find("a") != std::string::npos);
    CHECK(text.find("b") != std::string::npos);
}

TEST(html_nested_content_and_punctuation) {
    const std::string html = "<p>Hello <b>world</b>!</p><p>Second.</p>";
    CHECK_EQ(htmlStripTags(html), "Hello world! Second.");
}

TEST(html_title_falls_back_to_url_ok) {
    // No <title> -> empty title (caller falls back to the URL).
    CHECK(htmlExtractTitle("<html><body>no title</body></html>").empty());
}
