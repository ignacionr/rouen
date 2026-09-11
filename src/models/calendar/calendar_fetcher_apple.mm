#import <Foundation/Foundation.h>
#import <EventKit/EventKit.h>
#include "calendar_fetcher_apple.h"

namespace calendar {

static std::string escape_json_str(NSString *nsStr) {
    if (!nsStr) return "";
    return std::string([nsStr UTF8String]);
}

std::vector<event> fetch_events_apple(const std::string& start_date_iso, const std::string& end_date_iso, std::string& out_error) {
    out_error.clear();
    std::vector<event> result;

    @autoreleasepool {
        EKEventStore *store = [[EKEventStore alloc] init];
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        __block BOOL access = NO;

        if (@available(macOS 14.0, *)) {
            [store requestFullAccessToEventsWithCompletion:^(BOOL granted, NSError * _Nullable error) {
                access = granted;
                dispatch_semaphore_signal(sema);
            }];
        } else {
            [store requestAccessToEntityType:EKEntityTypeEvent completion:^(BOOL granted, NSError * _Nullable error) {
                access = granted;
                dispatch_semaphore_signal(sema);
            }];
        }

        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(sema, timeout) != 0) {
            out_error = "Timeout waiting for Calendar permissions";
            return result;
        }

        if (!access) {
            out_error = "Permission denied to access macOS Calendar";
            return result;
        }

        NSDateFormatter *isoFormatter = [[NSDateFormatter alloc] init];
        [isoFormatter setDateFormat:@"yyyy-MM-dd'T'HH:mm:ss"];
        [isoFormatter setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];

        NSDateFormatter *dateOnlyFormatter = [[NSDateFormatter alloc] init];
        [dateOnlyFormatter setDateFormat:@"yyyy-MM-dd"];
        [dateOnlyFormatter setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];

        NSDate *now = [NSDate date];
        NSDate *startDate = [now dateByAddingTimeInterval:-7 * 86400];
        NSDate *endDate = [now dateByAddingTimeInterval:14 * 86400];

        if (!start_date_iso.empty()) {
            NSString *sStr = [NSString stringWithUTF8String:start_date_iso.c_str()];
            NSDate *parsed = [isoFormatter dateFromString:sStr];
            if (!parsed) parsed = [dateOnlyFormatter dateFromString:sStr];
            if (parsed) startDate = parsed;
        }

        if (!end_date_iso.empty()) {
            NSString *eStr = [NSString stringWithUTF8String:end_date_iso.c_str()];
            NSDate *parsed = [isoFormatter dateFromString:eStr];
            if (!parsed) parsed = [dateOnlyFormatter dateFromString:eStr];
            if (parsed) {
                if (end_date_iso.length() <= 10) {
                    NSCalendar *cal = [NSCalendar currentCalendar];
                    endDate = [cal dateBySettingHour:23 minute:59 second:59 ofDate:parsed options:0];
                } else {
                    endDate = parsed;
                }
            }
        }

        NSPredicate *pred = [store predicateForEventsWithStartDate:startDate endDate:endDate calendars:nil];
        NSArray<EKEvent *> *events = [store eventsMatchingPredicate:pred];

        for (EKEvent *ekEvent in events) {
            event ev;
            ev.id = escape_json_str(ekEvent.eventIdentifier);
            ev.summary = escape_json_str(ekEvent.title);
            ev.description = escape_json_str(ekEvent.notes);
            ev.location = escape_json_str(ekEvent.location);
            ev.all_day = ekEvent.isAllDay;

            if (ekEvent.startDate) {
                ev.start = std::string([[isoFormatter stringFromDate:ekEvent.startDate] UTF8String]);
            }
            if (ekEvent.endDate) {
                ev.end = std::string([[isoFormatter stringFromDate:ekEvent.endDate] UTF8String]);
            }

            if (ekEvent.organizer && ekEvent.organizer.name) {
                ev.organizer = escape_json_str(ekEvent.organizer.name);
            }

            result.push_back(std::move(ev));
        }
    }

    return result;
}

bool create_event_apple(const std::string& calendar_name, const std::string& summary, const std::string& description, const std::string& location,
                        int start_year, int start_month, int start_day, int start_hour, int start_min,
                        int end_year, int end_month, int end_day, int end_hour, int end_min,
                        bool is_all_day, std::string& out_error) {
    out_error.clear();
    @autoreleasepool {
        EKEventStore *store = [[EKEventStore alloc] init];
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);
        __block BOOL access = NO;

        if (@available(macOS 14.0, *)) {
            [store requestFullAccessToEventsWithCompletion:^(BOOL granted, NSError * _Nullable error) {
                access = granted;
                dispatch_semaphore_signal(sema);
            }];
        } else {
            [store requestAccessToEntityType:EKEntityTypeEvent completion:^(BOOL granted, NSError * _Nullable error) {
                access = granted;
                dispatch_semaphore_signal(sema);
            }];
        }

        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(sema, timeout) != 0 || !access) {
            out_error = "Permission denied or timeout accessing macOS Calendar";
            return false;
        }

        EKEvent *event = [EKEvent eventWithEventStore:store];
        event.title = [NSString stringWithUTF8String:summary.c_str()];
        if (!description.empty()) event.notes = [NSString stringWithUTF8String:description.c_str()];
        if (!location.empty()) event.location = [NSString stringWithUTF8String:location.c_str()];
        event.allDay = is_all_day;

        NSCalendar *gregorian = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
        NSDateComponents *startComps = [[NSDateComponents alloc] init];
        startComps.year = start_year;
        startComps.month = start_month;
        startComps.day = start_day;
        startComps.hour = start_hour;
        startComps.minute = start_min;
        event.startDate = [gregorian dateFromComponents:startComps];

        NSDateComponents *endComps = [[NSDateComponents alloc] init];
        endComps.year = end_year;
        endComps.month = end_month;
        endComps.day = end_day;
        endComps.hour = end_hour;
        endComps.minute = end_min;
        event.endDate = [gregorian dateFromComponents:endComps];

        EKCalendar *targetCal = [store defaultCalendarForNewEvents];
        if (!calendar_name.empty()) {
            NSArray<EKCalendar *> *cals = [store calendarsForEntityType:EKEntityTypeEvent];
            for (EKCalendar *c in cals) {
                if ([[c title] isEqualToString:[NSString stringWithUTF8String:calendar_name.c_str()]]) {
                    targetCal = c;
                    break;
                }
            }
        }

        event.calendar = targetCal;

        NSError *err = nil;
        BOOL success = [store saveEvent:event span:EKSpanThisEvent commit:YES error:&err];
        if (!success && err) {
            out_error = [[err localizedDescription] UTF8String];
            return false;
        }
        return true;
    }
}

} // namespace calendar
