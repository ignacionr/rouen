#!/usr/bin/env swift
// fetch_calendar.swift — uses EventKit so recurring events are expanded correctly.
// Replace the fetch_calendar.scpt usage:
//   swift /path/to/fetch_calendar.swift

import EventKit
import Foundation

// ── helpers ─────────────────────────────────────────────────────────────────

func escapeJSON(_ s: String) -> String {
    var out = ""
    for ch in s.unicodeScalars {
        switch ch.value {
        case 0x22: out += "\\\""   // "
        case 0x5C: out += "\\\\"  // \
        case 0x0A: out += "\\n"
        case 0x0D: out += "\\r"
        case 0x09: out += "\\t"
        default:
            if ch.value < 0x20 {
                out += String(format: "\\u%04X", ch.value)
            } else {
                out += String(ch)
            }
        }
    }
    return out
}

let iso: DateFormatter = {
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd'T'HH:mm:ss"
    f.locale = Locale(identifier: "en_US_POSIX")
    return f
}()

let isoDate: DateFormatter = {
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd"
    f.locale = Locale(identifier: "en_US_POSIX")
    return f
}()

// ── EventKit setup ───────────────────────────────────────────────────────────

let store = EKEventStore()
let sema = DispatchSemaphore(value: 0)
var accessGranted = false

if #available(macOS 14.0, *) {
    store.requestFullAccessToEvents { granted, _ in
        accessGranted = granted
        sema.signal()
    }
} else {
    store.requestAccess(to: .event) { granted, _ in
        accessGranted = granted
        sema.signal()
    }
}
sema.wait()

guard accessGranted else {
    fputs("Permission denied to access calendars\n", stderr)
    exit(1)
}

// ── Date range ───────────────────────────────────────────────────────────────

func parseDate(_ s: String) -> Date? {
    if let d = isoDate.date(from: s) { return d }
    return iso.date(from: s)
}

let now = Date()
var startDate = Calendar.current.date(byAdding: .day, value: -7, to: now)!
var endDate   = Calendar.current.date(byAdding: .day, value: 14,  to: now)!

if CommandLine.arguments.count >= 3 {
    if let sDate = parseDate(CommandLine.arguments[1]) {
        startDate = sDate
    }
    if let eDate = parseDate(CommandLine.arguments[2]) {
        endDate = eDate
    }
}

// ── Fetch events (EventKit expands recurring occurrences automatically) ───────

let predicate = store.predicateForEvents(withStart: startDate, end: endDate, calendars: nil)

var items: [String] = []

store.enumerateEvents(matching: predicate) { ekEvent, _ in
    let evId      = ekEvent.eventIdentifier ?? ""
    let summary   = ekEvent.title ?? ""
    let desc      = ekEvent.notes ?? ""
    let location  = ekEvent.location ?? ""
    let isAllDay  = ekEvent.isAllDay
    let startDt   = ekEvent.startDate!
    let endDt     = ekEvent.endDate!

    var obj = "{"
    obj += "\"id\":\"\(escapeJSON(evId))\""
    obj += ",\"summary\":\"\(escapeJSON(summary))\""
    obj += ",\"description\":\"\(escapeJSON(desc))\""
    obj += ",\"location\":\"\(escapeJSON(location))\""
    obj += ",\"htmlLink\":\"\""   // no URL in local calendars

    if isAllDay {
        let s = isoDate.string(from: startDt)
        let e = isoDate.string(from: endDt)
        obj += ",\"start\":{\"date\":\"\(s)\"}"
        obj += ",\"end\":{\"date\":\"\(e)\"}"
    } else {
        let s = iso.string(from: startDt)
        let e = iso.string(from: endDt)
        obj += ",\"start\":{\"dateTime\":\"\(s)\"}"
        obj += ",\"end\":{\"dateTime\":\"\(e)\"}"
    }
    obj += "}"
    items.append(obj)
}

// ── Writable calendars ────────────────────────────────────────────────────────

let writableCalendars = store.calendars(for: .event)
    .filter { $0.allowsContentModifications }
    .map { "\"\(escapeJSON($0.title))\"" }

// ── Output ───────────────────────────────────────────────────────────────────

let json = "{\"items\":[\(items.joined(separator: ","))],\"calendars\":[\(writableCalendars.joined(separator: ","))]}"
print(json)
