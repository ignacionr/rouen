on run argv
    if (count of argv) < 15 then
        return "ERROR: Missing arguments"
    end if
    
    set calName to item 1 of argv
    set evSummary to item 2 of argv
    set evDesc to item 3 of argv
    set evLoc to item 4 of argv
    
    set startYear to (item 5 of argv) as integer
    set startMonth to (item 6 of argv) as integer
    set startDay to (item 7 of argv) as integer
    set startHour to (item 8 of argv) as integer
    set startMin to (item 9 of argv) as integer
    
    set endYear to (item 10 of argv) as integer
    set endMonth to (item 11 of argv) as integer
    set endDay to (item 12 of argv) as integer
    set endHour to (item 13 of argv) as integer
    set endMin to (item 14 of argv) as integer
    
    set isAllDayStr to item 15 of argv
    set isAllDay to false
    if isAllDayStr is "true" then
        set isAllDay to true
    end if
    
    set startDate to (current date)
    set year of startDate to startYear
    set month of startDate to startMonth
    set day of startDate to startDay
    set time of startDate to (startHour * 3600 + startMin * 60)
    
    set endDate to (current date)
    set year of endDate to endYear
    set month of endDate to endMonth
    set day of endDate to endDay
    set time of endDate to (endHour * 3600 + endMin * 60)
    
    try
        tell application "Calendar"
            tell calendar calName
                make new event with properties {summary:evSummary, start date:startDate, end date:endDate, description:evDesc, location:evLoc, allday event:isAllDay}
            end tell
        end tell
        return "SUCCESS"
    on error errMsg
        return "ERROR: " & errMsg
    end try
end run
