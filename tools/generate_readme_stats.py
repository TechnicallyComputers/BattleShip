#!/usr/bin/env python3
"""Generate README SVG charts from GitHub repository metadata."""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import html
import json
import math
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path


API_ROOT = "https://api.github.com"
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
PLATFORMS = ("macOS", "Windows", "Linux", "Android", "Other")
REGIONS = ("US", "JP")
DOWNLOAD_SERIES = tuple(
    (platform, region)
    for platform in PLATFORMS
    for region in REGIONS
)
CHART_THEMES = {
    "light": {
        "surface": "#ffffff",
        "border": "#d0d7de",
        "plot": "#f6f8fa",
        "title": "#24292f",
        "subtitle": "#57606a",
        "axis": "#57606a",
        "grid": "#d8dee4",
        "star_line": "#fb8500",
        "star_fill_top": "#ffd08a",
        "star_fill_bottom": "#fff7eb",
        "downloads": {
            ("macOS", "US"): "#69b7ff",
            ("macOS", "JP"): "#0b6fd3",
            ("Windows", "US"): "#63d471",
            ("Windows", "JP"): "#23883a",
            ("Linux", "US"): "#d9dee7",
            ("Linux", "JP"): "#697386",
            ("Android", "US"): "#ffb061",
            ("Android", "JP"): "#d85a05",
            ("Other", "US"): "#afb8c1",
            ("Other", "JP"): "#57606a",
        },
    },
    "dark": {
        "surface": "#0d1117",
        "border": "#30363d",
        "plot": "#161b22",
        "title": "#f0f6fc",
        "subtitle": "#8b949e",
        "axis": "#8b949e",
        "grid": "#30363d",
        "star_line": "#ffa657",
        "star_fill_top": "#9e6a03",
        "star_fill_bottom": "#161b22",
        "downloads": {
            ("macOS", "US"): "#79c0ff",
            ("macOS", "JP"): "#2f81f7",
            ("Windows", "US"): "#7ee787",
            ("Windows", "JP"): "#3fb950",
            ("Linux", "US"): "#c9d1d9",
            ("Linux", "JP"): "#8b949e",
            ("Android", "US"): "#ffb77c",
            ("Android", "JP"): "#f0883e",
            ("Other", "US"): "#afb8c1",
            ("Other", "JP"): "#6e7681",
        },
    },
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        default=os.environ.get("GITHUB_REPOSITORY", "JRickey/BattleShip"),
        help="GitHub repository in owner/name form",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/readme-stats"),
        help="Directory for generated SVG files",
    )
    return parser.parse_args()


def github_token() -> str | None:
    return os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")


def parse_link_header(header: str) -> dict[str, str]:
    links: dict[str, str] = {}
    for part in header.split(","):
        match = re.match(r'\s*<([^>]+)>;\s*rel="([^"]+)"', part)
        if match:
            links[match.group(2)] = match.group(1)
    return links


def api_request(url: str, accept: str) -> tuple[object, str]:
    request = urllib.request.Request(url)
    request.add_header("Accept", accept)
    request.add_header("User-Agent", "battleship-readme-stats")
    request.add_header("X-GitHub-Api-Version", "2022-11-28")
    token = github_token()
    if token:
        request.add_header("Authorization", f"Bearer {token}")

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = json.loads(response.read().decode("utf-8"))
            return payload, response.headers.get("Link", "")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API request failed: {exc.code} {url}\n{body}") from exc


def get_json(path: str, accept: str = "application/vnd.github+json") -> object:
    url = path if path.startswith("https://") else f"{API_ROOT}{path}"
    payload, _ = api_request(url, accept)
    return payload


def get_pages(path: str, accept: str = "application/vnd.github+json") -> list[object]:
    url: str | None = f"{API_ROOT}{path}"
    items: list[object] = []
    while url:
        payload, link_header = api_request(url, accept)
        if not isinstance(payload, list):
            raise RuntimeError(f"Expected list response from {url}")
        items.extend(payload)
        url = parse_link_header(link_header).get("next")
    return items


def parse_date(value: str) -> dt.date:
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00")).date()


def format_int(value: int) -> str:
    return f"{value:,}"


def format_axis_value(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M".replace(".0M", "M")
    if value >= 1_000:
        return f"{value / 1_000:.1f}k".replace(".0k", "k")
    return str(int(value))


def format_date_label(value: dt.date) -> str:
    return f"{MONTHS[value.month - 1]} {value.day}, {value.year}"


def nice_max(value: int) -> int:
    if value <= 0:
        return 1
    exponent = math.floor(math.log10(value))
    fraction = value / (10 ** exponent)
    if fraction <= 1:
        nice = 1
    elif fraction <= 2:
        nice = 2
    elif fraction <= 3:
        nice = 3
    elif fraction <= 4:
        nice = 4
    elif fraction <= 5:
        nice = 5
    else:
        nice = 10
    return int(nice * (10 ** exponent))


def date_ticks(start: dt.date, end: dt.date, count: int = 4) -> list[dt.date]:
    if start >= end:
        return [start]
    span = (end - start).days
    return [start + dt.timedelta(days=round(span * i / (count - 1))) for i in range(count)]


def svg_header(width: int, height: int, title: str, desc: str, theme: dict[str, object]) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        f"<title id=\"title\">{html.escape(title)}</title>",
        f"<desc id=\"desc\">{html.escape(desc)}</desc>",
        "<style>",
        "svg { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; }",
        f".surface {{ fill: {theme['surface']}; stroke: {theme['border']}; }}",
        f".plot-bg {{ fill: {theme['plot']}; }}",
        f".title {{ fill: {theme['title']}; font-size: 24px; font-weight: 700; }}",
        f".subtitle {{ fill: {theme['subtitle']}; font-size: 14px; }}",
        f".axis {{ fill: {theme['axis']}; font-size: 12px; }}",
        f".grid {{ stroke: {theme['grid']}; stroke-width: 1; }}",
        f".legend {{ fill: {theme['title']}; font-size: 13px; }}",
        f".stat {{ fill: {theme['title']}; font-size: 22px; font-weight: 700; }}",
        f".stat-label {{ fill: {theme['subtitle']}; font-size: 12px; }}",
        "</style>",
        f'<rect class="surface" x="0.5" y="0.5" width="{width - 1}" '
        f'height="{height - 1}" rx="10"/>',
    ]


def svg_footer() -> list[str]:
    return ["</svg>"]


def xy_for_date(
    value: dt.date,
    start: dt.date,
    end: dt.date,
    plot_x: int,
    plot_w: int,
) -> float:
    span = max((end - start).days, 1)
    return plot_x + ((value - start).days / span) * plot_w


def y_for_value(value: float, y_max: float, plot_y: int, plot_h: int) -> float:
    return plot_y + plot_h - (value / y_max) * plot_h


def polyline_path(points: list[tuple[float, float]]) -> str:
    if not points:
        return ""
    head, *tail = points
    return " ".join([f"M {head[0]:.2f} {head[1]:.2f}"] +
                    [f"L {x:.2f} {y:.2f}" for x, y in tail])


def draw_grid(
    lines: list[str],
    start: dt.date,
    end: dt.date,
    y_max: int,
    plot_x: int,
    plot_y: int,
    plot_w: int,
    plot_h: int,
) -> None:
    lines.append(f'<rect class="plot-bg" x="{plot_x}" y="{plot_y}" '
                 f'width="{plot_w}" height="{plot_h}" rx="6"/>')
    for i in range(5):
        value = y_max * i / 4
        y = y_for_value(value, y_max, plot_y, plot_h)
        lines.append(f'<line class="grid" x1="{plot_x}" y1="{y:.2f}" '
                     f'x2="{plot_x + plot_w}" y2="{y:.2f}"/>')
        lines.append(f'<text class="axis" x="{plot_x - 12}" y="{y + 4:.2f}" '
                     f'text-anchor="end">{format_axis_value(value)}</text>')

    for tick in date_ticks(start, end):
        x = xy_for_date(tick, start, end, plot_x, plot_w)
        lines.append(f'<text class="axis" x="{x:.2f}" y="{plot_y + plot_h + 34}" '
                     f'text-anchor="middle">{html.escape(format_date_label(tick))}</text>')


def build_star_points(repo: str) -> tuple[dt.date, list[tuple[dt.date, int]], int]:
    repo_data = get_json(f"/repos/{repo}")
    if not isinstance(repo_data, dict):
        raise RuntimeError("Unexpected repository metadata response")
    created = parse_date(str(repo_data["created_at"]))

    stargazers = get_pages(
        f"/repos/{repo}/stargazers?per_page=100",
        accept="application/vnd.github.star+json",
    )
    dates: list[dt.date] = []
    for entry in stargazers:
        if isinstance(entry, dict) and entry.get("starred_at"):
            dates.append(parse_date(str(entry["starred_at"])))

    daily = collections.Counter(dates)
    points: list[tuple[dt.date, int]] = [(created, 0)]
    total = 0
    for day in sorted(daily):
        total += daily[day]
        points.append((day, total))

    api_count = int(repo_data.get("stargazers_count", total))
    if points and api_count > points[-1][1]:
        points.append((dt.date.today(), api_count))
        total = api_count

    return created, points, total


def render_stars(repo: str, mode: str) -> str:
    theme = CHART_THEMES[mode]
    start, points, total = build_star_points(repo)
    end = max((day for day, _ in points), default=start)
    y_max = nice_max(total)
    width, height = 960, 420
    plot_x, plot_y, plot_w, plot_h = 72, 100, 840, 250

    lines = svg_header(
        width,
        height,
        "GitHub star growth",
        f"{repo} has {format_int(total)} stars in the current GitHub API data.",
        theme,
    )
    lines.extend([
        "<defs>",
        '<linearGradient id="starFill" x1="0" y1="0" x2="0" y2="1">',
        f'<stop offset="0%" stop-color="{theme["star_fill_top"]}" stop-opacity="0.62"/>',
        f'<stop offset="100%" stop-color="{theme["star_fill_bottom"]}" stop-opacity="0.12"/>',
        "</linearGradient>",
        "</defs>",
        '<text class="title" x="36" y="40">GitHub star growth</text>',
        f'<text class="subtitle" x="36" y="64">Since repository creation</text>',
        f'<text class="stat" x="902" y="39" text-anchor="end">{format_int(total)}</text>',
        '<text class="stat-label" x="902" y="61" text-anchor="end">stars</text>',
    ])
    draw_grid(lines, start, end, y_max, plot_x, plot_y, plot_w, plot_h)

    chart_points = [
        (
            xy_for_date(day, start, end, plot_x, plot_w),
            y_for_value(value, y_max, plot_y, plot_h),
        )
        for day, value in points
    ]
    if chart_points:
        first_x, _ = chart_points[0]
        last_x, _ = chart_points[-1]
        baseline = plot_y + plot_h
        area = [f"M {first_x:.2f} {baseline:.2f}"] + [
            f"L {x:.2f} {y:.2f}" for x, y in chart_points
        ] + [f"L {last_x:.2f} {baseline:.2f}", "Z"]
        lines.append(f'<path d="{" ".join(area)}" fill="url(#starFill)"/>')
        lines.append(f'<path d="{polyline_path(chart_points)}" fill="none" '
                     f'stroke="{theme["star_line"]}" stroke-width="3" '
                     'stroke-linejoin="round" stroke-linecap="round"/>')
        last_x, last_y = chart_points[-1]
        lines.append(f'<circle cx="{last_x:.2f}" cy="{last_y:.2f}" r="4.5" '
                     f'fill="{theme["star_line"]}"/>')

    lines.extend(svg_footer())
    return "\n".join(lines) + "\n"


def classify_download_asset(asset_name: str) -> tuple[str, str] | None:
    lower = asset_name.lower()
    if "sha256" in lower or lower.endswith(".txt"):
        return None
    region = "JP" if re.search(r"(^|[-_.])jp([-_.]|$)", lower) else "US"
    if lower.endswith(".apk"):
        return ("Android", region)
    if "appimage" in lower:
        return ("Linux", region)
    if lower.endswith(".dmg"):
        return ("macOS", region)
    if "windows" in lower or lower.endswith(".zip"):
        return ("Windows", region)
    return ("Other", region)


def release_label_ticks(count: int) -> set[int]:
    if count <= 1:
        return {0}
    step = 4 if count <= 32 else max(1, count // 8)
    ticks = set(range(0, count, step))
    ticks.add(count - 1)
    return ticks


def x_for_release(index: int, count: int, plot_x: int, plot_w: int) -> float:
    span = max(count - 1, 1)
    return plot_x + (index / span) * plot_w


def draw_release_grid(
    lines: list[str],
    tags: list[str],
    y_max: int,
    plot_x: int,
    plot_y: int,
    plot_w: int,
    plot_h: int,
) -> None:
    lines.append(f'<rect class="plot-bg" x="{plot_x}" y="{plot_y}" '
                 f'width="{plot_w}" height="{plot_h}" rx="6"/>')
    for i in range(5):
        value = y_max * i / 4
        y = y_for_value(value, y_max, plot_y, plot_h)
        lines.append(f'<line class="grid" x1="{plot_x}" y1="{y:.2f}" '
                     f'x2="{plot_x + plot_w}" y2="{y:.2f}"/>')
        lines.append(f'<text class="axis" x="{plot_x - 12}" y="{y + 4:.2f}" '
                     f'text-anchor="end">{format_axis_value(value)}</text>')

    for index in sorted(release_label_ticks(len(tags))):
        if index >= len(tags):
            continue
        x = x_for_release(index, len(tags), plot_x, plot_w)
        y = plot_y + plot_h + 42
        lines.append(f'<text class="axis" x="{x:.2f}" y="{y}" text-anchor="end" '
                     f'transform="rotate(-35 {x:.2f} {y})">{html.escape(tags[index])}</text>')


def build_download_points(repo: str) -> tuple[list[tuple[str, dt.date, dict[tuple[str, str], int]]], int, int]:
    releases = get_pages(f"/repos/{repo}/releases?per_page=100")
    release_counts: list[tuple[dt.date, str, collections.Counter[tuple[str, str]]]] = []

    for release in releases:
        if not isinstance(release, dict) or release.get("draft"):
            continue
        published_at = release.get("published_at") or release.get("created_at")
        if not published_at:
            continue
        day = parse_date(str(published_at))
        tag = str(release.get("tag_name") or format_date_label(day))
        counts: collections.Counter[tuple[str, str]] = collections.Counter()
        for asset in release.get("assets", []):
            if not isinstance(asset, dict):
                continue
            series = classify_download_asset(str(asset.get("name", "")))
            if series:
                counts[series] += int(asset.get("download_count") or 0)
        release_counts.append((day, tag, counts))

    cumulative = {series: 0 for series in DOWNLOAD_SERIES}
    points: list[tuple[str, dt.date, dict[tuple[str, str], int]]] = []
    for day, tag, counts in sorted(release_counts, key=lambda item: item[0]):
        for series, count in counts.items():
            cumulative[series] += count
        points.append((tag, day, dict(cumulative)))

    total_downloads = sum(points[-1][2].values()) if points else 0
    return points, len(release_counts), total_downloads


def render_downloads(repo: str, mode: str) -> str:
    theme = CHART_THEMES[mode]
    points, release_count, total_downloads = build_download_points(repo)
    width, height = 960, 580
    plot_x, plot_y, plot_w, plot_h = 72, 112, 840, 250

    if points:
        tags = [tag for tag, _day, _snapshot in points]
        y_max = nice_max(total_downloads)
    else:
        tags = []
        y_max = 1

    lines = svg_header(
        width,
        height,
        "Downloads by Platform",
        f"{repo} has {format_int(total_downloads)} release asset downloads "
        "in the current GitHub API data.",
        theme,
    )
    lines.extend([
        '<text class="title" x="36" y="40">Downloads by Platform</text>',
        '<text class="subtitle" x="36" y="64">Categorical x axis by release tag; US is lighter, JP is darker</text>',
        f'<text class="stat" x="902" y="39" text-anchor="end">{format_int(total_downloads)}</text>',
        f'<text class="stat-label" x="902" y="61" text-anchor="end">downloads across {release_count} releases</text>',
    ])
    draw_release_grid(lines, tags, y_max, plot_x, plot_y, plot_w, plot_h)

    totals_by_series = {
        series: (points[-1][2].get(series, 0) if points else 0)
        for series in DOWNLOAD_SERIES
    }

    lower = [0 for _ in points]
    for series in DOWNLOAD_SERIES:
        values = [snapshot.get(series, 0) for _tag, _day, snapshot in points]
        if not any(values):
            continue
        upper = [base + value for base, value in zip(lower, values)]
        upper_points = [
            (
                x_for_release(index, len(points), plot_x, plot_w),
                y_for_value(value, y_max, plot_y, plot_h),
            )
            for index, value in enumerate(upper)
        ]
        lower_points = [
            (
                x_for_release(index, len(points), plot_x, plot_w),
                y_for_value(value, y_max, plot_y, plot_h),
            )
            for index, value in enumerate(lower)
        ]
        path = (
            [f"M {upper_points[0][0]:.2f} {upper_points[0][1]:.2f}"] +
            [f"L {x:.2f} {y:.2f}" for x, y in upper_points[1:]] +
            [f"L {x:.2f} {y:.2f}" for x, y in reversed(lower_points)] +
            ["Z"]
        )
        color = theme["downloads"][series]  # type: ignore[index]
        lines.append(f'<path d="{" ".join(path)}" fill="{color}" fill-opacity="0.78"/>')
        lines.append(f'<path d="{polyline_path(upper_points)}" fill="none" '
                     f'stroke="{color}" stroke-width="1.5" stroke-opacity="0.95" '
                     'stroke-linejoin="round" stroke-linecap="round"/>')
        lower = upper

    legend_y = 466
    legend_x = 72
    for series in DOWNLOAD_SERIES:
        count = totals_by_series[series]
        if count == 0:
            continue
        platform, region = series
        color = theme["downloads"][series]  # type: ignore[index]
        label = f"{platform} {region} {format_int(count)}"
        item_width = 40 + (7 * len(label))
        if legend_x + item_width > 900:
            legend_x = 72
            legend_y += 26
        lines.append(f'<circle cx="{legend_x}" cy="{legend_y - 4}" r="5" fill="{color}"/>')
        lines.append(f'<text class="legend" x="{legend_x + 12}" y="{legend_y}">'
                     f'{html.escape(label)}</text>')
        legend_x += item_width

    lines.extend(svg_footer())
    return "\n".join(lines) + "\n"


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def main() -> int:
    args = parse_args()
    if "/" not in args.repo:
        raise SystemExit("--repo must be in owner/name form")

    args.output.mkdir(parents=True, exist_ok=True)
    for mode in ("light", "dark"):
        write_if_changed(args.output / f"star-growth-{mode}.svg", render_stars(args.repo, mode))
        write_if_changed(args.output / f"release-downloads-{mode}.svg", render_downloads(args.repo, mode))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
