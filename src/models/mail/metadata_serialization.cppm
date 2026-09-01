module;

#include "models/mail/metadata_serialization.hpp"

export module rouen.models.mail.metadata_serialization;

export namespace mail {
    using mail::serialize_tags;
    using mail::deserialize_tags;
    using mail::serialize_action_links;
    using mail::deserialize_action_links;
}
