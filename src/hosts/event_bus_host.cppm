module;

#include "event_bus_host.hpp"

export module rouen.hosts.event_bus_host;

export namespace rouen::events {
    using rouen::events::rouen_event;
}

export namespace rouen::hosts {
    using rouen::hosts::event_bus_host;
    using rouen::hosts::subscription_id;
    using rouen::hosts::event_callback;
    using rouen::hosts::topic_matches;
    using rouen::hosts::scoped_event_subscription;
}
