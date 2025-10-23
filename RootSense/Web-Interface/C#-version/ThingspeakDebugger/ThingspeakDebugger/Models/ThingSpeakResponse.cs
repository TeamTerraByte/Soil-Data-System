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
        [JsonPropertyName("field3")] public string? Field3Name { get; set; }
        [JsonPropertyName("field4")] public string? Field4Name { get; set; }
        [JsonPropertyName("field5")] public string? Field5Name { get; set; }
        [JsonPropertyName("field6")] public string? Field6Name { get; set; }
        [JsonPropertyName("field7")] public string? Field7Name { get; set; }
        [JsonPropertyName("field8")] public string? Field8Name { get; set; }
    }

    public class ThingSpeakFeed
    {
        [JsonPropertyName("created_at")] public DateTimeOffset? CreatedAt { get; set; }
        [JsonPropertyName("entry_id")] public int? EntryId { get; set; }
        [JsonPropertyName("field1")] public string? Field1 { get; set; }
        [JsonPropertyName("field2")] public string? Field2 { get; set; }
        [JsonPropertyName("field3")] public string? Field3 { get; set; }
        [JsonPropertyName("field4")] public string? Field4 { get; set; }
        [JsonPropertyName("field5")] public string? Field5 { get; set; }
        [JsonPropertyName("field6")] public string? Field6 { get; set; }
        [JsonPropertyName("field7")] public string? Field7 { get; set; }
        [JsonPropertyName("field8")] public string? Field8 { get; set; }
    }
}
