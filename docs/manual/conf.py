# Configuration file for the Sphinx documentation builder.
# DeepLearningTool 离线用户手册配置

project = "DeepLearningTool"
copyright = "DeepLearningTool"
author = "DeepLearningTool Team"

extensions = [
    "myst_parser",
]

source_suffix = {
    ".md": "markdown",
}

language = "zh_CN"
html_search_language = "zh"

html_theme = "furo"
html_title = "DeepLearningTool 帮助文档"
html_copy_source = False
html_show_sourcelink = False
html_show_search_summary = False

html_theme_options = {
    "light_css_variables": {
        "color-brand-primary": "#009688",
        "color-brand-content": "#009688",
    },
    "dark_css_variables": {
        "color-brand-primary": "#009688",
        "color-brand-content": "#009688",
    },
}

html_static_path = ["_static"]
html_js_files = ["chinese_search.js"]
