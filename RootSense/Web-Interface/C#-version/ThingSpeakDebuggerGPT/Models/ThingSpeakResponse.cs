using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace ThingSpeakDebugger.Models
{
    public class ThingSpeakResponse
    {
        [JsonPropertyName("channel")]
        public ThingSpeakChannel? Channel { get; set; }

        [JsonPropertyName("feeds")]
        public List<ThingSpeakFeed>? Feeds { get; set; }
    }

    public class ThingSpeakChannel
    {
        [JsonPropertyName("id")] public int Id { get; set; }
        [JsonPropertyName("name")] public string? Name { get; set; }
        [JsonPropertyName("field1")] public string? Field1Name { get; set; }
        [JsonPropertyName("field2")] public string? Field2Name { get; set; }
    }

    public class ThingSpeakFeed
    {
        [JsonPropertyName("created_at")] public DateTimeOffset? CreatedAt { get; set; }
        [JsonPropertyName("entry_id")] public int? EntryId { get; set; }
        [JsonPropertyName("field1")] public string? Field1 { get; set; }
        [JsonPropertyName("field2")] public string? Field2 { get; set; }
    }
}
