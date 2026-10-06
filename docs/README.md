# Documentation

Project documentation for Concrete Jungle. Start with the [architecture overview](architecture.md); the design documents go into each subsystem in depth.

## Overview

| Document | Read it when you want to… |
|---|---|
| [Architecture](architecture.md) | understand how the modules fit together, the frame lifecycle, units and conventions |
| [Testing](testing.md) | run the automated scenarios and interpret their metrics |
| [Backlog](backlog.md) | see what is planned, its priority and acceptance criteria |

## Design documents

How each subsystem works, why it works that way, and which constants tune it.

| Document | Subsystem |
|---|---|
| [City](design/city.md) | Procedural layout, buildings, special structures, street furniture, traffic signals |
| [Rendering](design/rendering.md) | Camera, render passes, day/night lighting, particles, HUD |
| [Vehicles](design/vehicles.md) | Vehicle classes, handling model, damage, fire and explosions |
| [CJ-002 handling proposal](design/vehicle-handling-proposal.md) | Approved class targets, source research, collision criteria and before/after measurements |
| [CJ-002 arcade baseline](design/vehicle-handling-baseline.md) | Recorded handling results for all 17 classes, collision failures and evidence provenance |
| [Physics](design/physics.md) | Collision detection, contact solver, breakaway objects, crash consequences |
| [Traffic](design/traffic.md) | Lane-following traffic, junction rules, cooperative yielding, recovery after crashes, driver incidents, police driving |
| [CJ-016 traffic behaviour proposal](design/traffic-behaviour-proposal.md) | Approved staged specification for physical manoeuvres, cooperative recovery, persistent drivers, incidents and CPU/test targets |
| [CJ-016 recovery results](design/traffic-recovery-results.md) | Frozen before/after recovery measurements, accepted isolated cases, exact evidence fingerprints and remaining scope limits |
| [CJ-016 yielding and incident results](design/traffic-yielding-incident-results.md) | Recovery CPU peaks, cooperative yielding and driver incident fixtures before/after, city CPU, defects found and remaining limits |
| [CJ-016 third increment results](design/traffic-third-increment-results.md) | Driver decision CPU under the targets in every city scenario, what the optimisations preserve, reproducible on-foot runs, wait-for loops (knocked pairs, junction gridlock) before/after |
| [Pedestrians](design/pedestrians.md) | Sidewalk behaviour, crossings, perception and dodging, fleeing, fighting back, steering, injuries, population |
| [Gameplay](design/gameplay.md) | Player, weapons, wanted level, police response, missions, pickups |
| [Audio](design/audio.md) | Procedural sound synthesis and file overrides |

## Guides

| Document | Task |
|---|---|
| [Building](guides/building.md) | Toolchain setup, build options, troubleshooting |
| [Adding content](guides/adding-content.md) | New vehicles, characters, weapons, foliage and sounds without code changes |

## Decision records

[Architecture decision records](adr/README.md) capture the significant technical decisions, the context they were made in and their consequences. Read the relevant record before changing a subsystem's fundamental approach.

## Project files

| File | Contents |
|---|---|
| [README](../README.md) | Project overview, quick start, controls |
| [CHANGELOG](../CHANGELOG.md) | Notable changes per milestone |
| [CONTRIBUTING](../CONTRIBUTING.md) | Workflow, coding style, commit and documentation conventions |
| [CREDITS](../CREDITS.md) | Third-party assets and licences |
| [CLAUDE](../CLAUDE.md), [AGENTS](../AGENTS.md) | Working notes for AI coding agents (`AGENTS.md` points to `CLAUDE.md`) |
