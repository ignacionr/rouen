on replaceText(find, replace, subject)
    set prevTIDs to text item delimiters of AppleScript
    set text item delimiters of AppleScript to find
    set theItems to text items of subject
    set text item delimiters of AppleScript to replace
    set theResult to theItems as string
    set text item delimiters of AppleScript to prevTIDs
    return theResult
end replaceText

on escapeJSON(str)
    set str to replaceText("\\", "\\\\", str)
    set str to replaceText("\"", "\\\"", str)
    set str to replaceText(character id 10, "\\n", str)
    set str to replaceText(character id 13, "\\r", str)
    set str to replaceText(character id 9, "\\t", str)
    return str
end escapeJSON

on dateToISO(dt)
    set y to year of dt as integer
    set m to (month of dt as integer)
    set d to day of dt as integer
    set t to time of dt
    set h to t div 3600
    set min to (t mod 3600) div 60
    set s to t mod 60
    
    set yStr to y as string
    if m < 10 then
        set mStr to "0" & (m as string)
    else
        set mStr to m as string
    end if
    if d < 10 then
        set dStr to "0" & (d as string)
    else
        set dStr to d as string
    end if
    if h < 10 then
        set hStr to "0" & (h as string)
    else
        set hStr to h as string
    end if
    if min < 10 then
        set minStr to "0" & (min as string)
    else
        set minStr to min as string
    end if
    if s < 10 then
        set sStr to "0" & (s as string)
    else
        set sStr to s as string
    end if
    return yStr & "-" & mStr & "-" & dStr & "T" & hStr & ":" & minStr & ":" & sStr
end dateToISO

on parseISODate(dateStr)
    try
        set y to text 1 thru 4 of dateStr as integer
        set m to text 6 thru 7 of dateStr as integer
        set d to text 9 thru 10 of dateStr as integer
        
        set dt to (current date)
        set day of dt to 1
        set year of dt to y
        set month of dt to m
        set day of dt to d
        set time of dt to 0
        
        if (length of dateStr) >= 19 then
            set h to text 12 thru 13 of dateStr as integer
            set min to text 15 thru 16 of dateStr as integer
            set s to text 18 thru 19 of dateStr as integer
            set time of dt to (h * 3600 + min * 60 + s)
        end if
        return dt
    on error
        return (current date)
    end try
end parseISODate

on run argv
    set today to (current date)
    set startDate to today - (7 * 24 * 60 * 60)
    set endDate to today + (14 * 24 * 60 * 60)
    
    if (count of argv) >= 2 then
        set startDate to my parseISODate(item 1 of argv)
        set endDate to my parseISODate(item 2 of argv)
    end if
    
    set jsonOutput to "{\"items\":["
    
    tell application "Calendar"
        set firstEvent to true
        repeat with aCal in calendars
            try
                set theEvents to (every event of aCal whose start date is greater than startDate and start date is less than endDate)
                repeat with anEvent in theEvents
                    set evId to id of anEvent
                    set evSummary to summary of anEvent
                    set evDesc to description of anEvent
                    if evDesc is missing value then set evDesc to ""
                    set evLoc to location of anEvent
                    if evLoc is missing value then set evLoc to ""
                    set evStart to start date of anEvent
                    set evEnd to end date of anEvent
                    set evAllDay to allday event of anEvent
                    
                    set evStartISO to my dateToISO(evStart)
                    set evEndISO to my dateToISO(evEnd)
                    
                    if not firstEvent then
                        set jsonOutput to jsonOutput & ","
                    else
                        set firstEvent to false
                    end if
                    
                    set jsonOutput to jsonOutput & "{\"id\":\"" & my escapeJSON(evId) & "\""
                    set jsonOutput to jsonOutput & ",\"summary\":\"" & my escapeJSON(evSummary) & "\""
                    set jsonOutput to jsonOutput & ",\"description\":\"" & my escapeJSON(evDesc) & "\""
                    set jsonOutput to jsonOutput & ",\"location\":\"" & my escapeJSON(evLoc) & "\""
                    
                    if evAllDay then
                        set evStartDateOnly to text 1 thru 10 of evStartISO
                        set evEndDateOnly to text 1 thru 10 of evEndISO
                        set jsonOutput to jsonOutput & ",\"start\":{\"date\":\"" & evStartDateOnly & "\"}"
                        set jsonOutput to jsonOutput & ",\"end\":{\"date\":\"" & evEndDateOnly & "\"}"
                    else
                        set jsonOutput to jsonOutput & ",\"start\":{\"dateTime\":\"" & evStartISO & "\"}"
                        set jsonOutput to jsonOutput & ",\"end\":{\"dateTime\":\"" & evEndISO & "\"}"
                    end if
                    set jsonOutput to jsonOutput & "}"
                end repeat
            end try
        end repeat
    end tell
    
    set jsonOutput to jsonOutput & "],\"calendars\":["
    tell application "Calendar"
        set firstCal to true
        repeat with aCal in calendars
            if writable of aCal is true then
                if not firstCal then
                    set jsonOutput to jsonOutput & ","
                else
                    set firstCal to false
                end if
                set jsonOutput to jsonOutput & "\"" & my escapeJSON(name of aCal) & "\""
            end if
        end repeat
    end tell
    set jsonOutput to jsonOutput & "]}"
    return jsonOutput
end run
