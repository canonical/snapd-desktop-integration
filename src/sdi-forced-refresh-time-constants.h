/*
 * Copyright (C) 2024 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define SECONDS_IN_A_DAY 86400
#define SECONDS_IN_AN_HOUR 3600
#define SECONDS_IN_A_MINUTE 60

// Time constants for forced refresh notifications.
#define TIME_TO_SHOW_REMAINING_TIME_BEFORE_FORCED_REFRESH (SECONDS_IN_A_DAY * 3)
#define TIME_TO_SHOW_ALERT_BEFORE_FORCED_REFRESH (SECONDS_IN_AN_HOUR * 19)
