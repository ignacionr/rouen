#!/bin/bash

# Deep RT RSS Analysis Script
# This script performs a comprehensive analysis of RT RSS content to understand media patterns

echo "=== Deep RT RSS Media Analysis ==="
echo "Fetching and analyzing RT RSS content..."

# Create temporary files
RSS_FILE="/tmp/rt_rss_content.xml"
ANALYSIS_FILE="/tmp/rt_analysis.txt"

# Fetch the RSS content
echo "1. Fetching RSS content from RT..."
curl -s "https://www.rt.com/rss/" > "$RSS_FILE"

if [ ! -s "$RSS_FILE" ]; then
    echo "ERROR: Failed to fetch RSS content"
    exit 1
fi

echo "   Downloaded $(wc -c < "$RSS_FILE") bytes"

# Basic RSS structure analysis
echo -e "\n2. RSS Structure Analysis:"
echo "   Feed title: $(grep -o '<title[^>]*>[^<]*</title>' "$RSS_FILE" | head -1 | sed 's/<[^>]*>//g')"
echo "   Total items: $(grep -c '<item>' "$RSS_FILE")"

# Look for content:encoded sections
echo -e "\n3. Content Analysis:"
CONTENT_ENCODED_COUNT=$(grep -c 'content:encoded' "$RSS_FILE")
echo "   Items with content:encoded: $CONTENT_ENCODED_COUNT"

# Extract and analyze a few content:encoded sections
echo -e "\n4. Sample content:encoded sections (first 3):"
grep -o '<content:encoded><!\[CDATA\[.*\]\]></content:encoded>' "$RSS_FILE" | head -3 | while IFS= read -r line; do
    echo "   --- Sample Content ---"
    # Remove CDATA wrapper and decode
    content=$(echo "$line" | sed 's/<content:encoded><!\[CDATA\[\(.*\)\]\]><\/content:encoded>/\1/')
    
    # Check for various media patterns
    echo "     Has iframe: $(echo "$content" | grep -c '<iframe')"
    echo "     Has video tag: $(echo "$content" | grep -c '<video')"
    echo "     Has audio tag: $(echo "$content" | grep -c '<audio')"
    echo "     Has .mp4 URLs: $(echo "$content" | grep -o 'https://[^"]*\.mp4[^"]*' | wc -l)"
    echo "     Has .mp3 URLs: $(echo "$content" | grep -o 'https://[^"]*\.mp3[^"]*' | wc -l)"
    echo "     Has mf.b37mrtl.ru URLs: $(echo "$content" | grep -o 'https://mf\.b37mrtl\.ru/[^"]*' | wc -l)"
    
    # Show iframe sources if any
    iframe_srcs=$(echo "$content" | grep -o '<iframe[^>]*src="[^"]*"' | grep -o 'src="[^"]*"' | sed 's/src="//;s/"//')
    if [ -n "$iframe_srcs" ]; then
        echo "     Iframe sources:"
        echo "$iframe_srcs" | while read -r src; do
            echo "       - $src"
        done
    fi
    echo ""
done

# Look for enclosures
echo -e "\n5. Enclosure Analysis:"
ENCLOSURE_COUNT=$(grep -c '<enclosure' "$RSS_FILE")
echo "   Total enclosures: $ENCLOSURE_COUNT"

echo "   Enclosure types:"
grep -o '<enclosure[^>]*type="[^"]*"' "$RSS_FILE" | grep -o 'type="[^"]*"' | sort | uniq -c

echo "   Sample enclosure URLs:"
grep -o '<enclosure[^>]*url="[^"]*"' "$RSS_FILE" | grep -o 'url="[^"]*"' | sed 's/url="//;s/"//' | head -5

# Look for media: namespace elements
echo -e "\n6. Media Namespace Analysis:"
echo "   media:content elements: $(grep -c 'media:content' "$RSS_FILE")"
echo "   media:thumbnail elements: $(grep -c 'media:thumbnail' "$RSS_FILE")"

# Search for video/audio file extensions in the entire RSS
echo -e "\n7. Media File Extension Analysis:"
echo "   .mp4 occurrences: $(grep -o '[^"]*\.mp4[^"]*' "$RSS_FILE" | wc -l)"
echo "   .mp3 occurrences: $(grep -o '[^"]*\.mp3[^"]*' "$RSS_FILE" | wc -l)"
echo "   .webm occurrences: $(grep -o '[^"]*\.webm[^"]*' "$RSS_FILE" | wc -l)"
echo "   .wav occurrences: $(grep -o '[^"]*\.wav[^"]*' "$RSS_FILE" | wc -l)"

# Show all unique media URLs found
echo -e "\n8. All Media URLs Found:"
grep -o 'https://[^"]*\.\(mp4\|mp3\|webm\|wav\|avi\|mov\)[^"]*' "$RSS_FILE" | sort | uniq | head -10

# Detailed analysis of first item with content:encoded
echo -e "\n9. Detailed Analysis of First Item:"
# Extract first item completely
FIRST_ITEM=$(sed -n '/<item>/,/<\/item>/p' "$RSS_FILE" | head -n -1 | tail -n +2)

echo "   Title: $(echo "$FIRST_ITEM" | grep -o '<title[^>]*>[^<]*</title>' | sed 's/<[^>]*>//g')"
echo "   Has description: $(echo "$FIRST_ITEM" | grep -c '<description>')"
echo "   Has content:encoded: $(echo "$FIRST_ITEM" | grep -c 'content:encoded')"
echo "   Has enclosure: $(echo "$FIRST_ITEM" | grep -c '<enclosure')"

if echo "$FIRST_ITEM" | grep -q 'content:encoded'; then
    echo "   Content:encoded analysis:"
    CONTENT=$(echo "$FIRST_ITEM" | grep -o '<content:encoded><!\[CDATA\[.*\]\]></content:encoded>' | sed 's/<content:encoded><!\[CDATA\[\(.*\)\]\]><\/content:encoded>/\1/')
    echo "     Content length: $(echo "$CONTENT" | wc -c) characters"
    echo "     Contains iframe: $(echo "$CONTENT" | grep -c '<iframe')"
    echo "     iframe sources:"
    echo "$CONTENT" | grep -o '<iframe[^>]*src="[^"]*"' | grep -o 'src="[^"]*"' | sed 's/src="//;s/"//' | while read -r src; do
        echo "       - $src"
    done
fi

# Look for YouTube or other video platform embeds
echo -e "\n10. Video Platform Analysis:"
echo "   YouTube embeds: $(grep -c 'youtube\.com' "$RSS_FILE")"
echo "   Vimeo embeds: $(grep -c 'vimeo\.com' "$RSS_FILE")"
echo "   Rumble embeds: $(grep -c 'rumble\.com' "$RSS_FILE")"

echo -e "\n=== Analysis Complete ==="
echo "Raw RSS file saved to: $RSS_FILE"
echo "Use 'cat $RSS_FILE' to view the complete content"
echo "Use 'grep -A 20 -B 5 \"<content:encoded>\" $RSS_FILE' to see content sections"
