module;

#include "models/rss/rss_url_resolver.hpp"

export module rouen.models.rss.rss_url_resolver;

export namespace rouen::hosts {
    using rouen::hosts::trim_copy;
    using rouen::hosts::resolve_relative_url;
    using rouen::hosts::resolve_youtube_url;
    using rouen::hosts::resolve_nyt_podcast_url;
    using rouen::hosts::extract_rss_url_from_html;
    using rouen::hosts::resolve_feed_url;
}
