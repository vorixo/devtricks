---
permalink: /about/77attempts
title: 77 Attempts
excerpt: Unknown Design, CPCRETRODEV
identifier: project
order: 3
website: https://cpcrulez.fr/GamesTest/77_attempts.htm
header:
    teaser: assets/images/projects/77Attempts.png
sidebar:
  - title: "Roles"
    text: "Code, graphics & music"
  - title: "Award"
    text: "[**#CPCRetroDev**](http://cpcretrodev.byterealms.com/) **2018**, Best Student Game"
  - title: "Date"
    text: "2018"
  - title: "Technologies"
    text: "Assembly, C, Zilog Z80 (4MHz), CPCtelera, Cygwin, Git"
  - title: "Platforms"
    text: "Amstrad CPC 464 (64KB)"
---

<iframe width="480" height="270" src="https://www.youtube.com/embed/5fCve9gS0gA" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

## Overview

"77 Attempts" is a [64K Amstrad CPC](https://en.wikipedia.org/wiki/Amstrad_CPC_464) video game powered by [CPCTelera](https://github.com/lronaldo/cpctelera). This platforming adventure draws inspiration from the iconic "[Super Meat Boy](https://store.steampowered.com/app/40800/Super_Meat_Boy)". Players assume the role of Alex, a charming character on a quest to rescue his beloved, the queen of gravity, who has been abducted by the nefarious villain, Attempo. Attempo seeks to harness her powers to reverse the planet's gravity. 

![Alex]({{ '/' | absolute_url }}/assets/images/projects/77Attempts/77Alex.jpg){: .align-center}

With Attempo conveniently on vacation, our hero seizes the opportunity to embark on a daring rescue mission. Along the way, Alex must outmaneuver Attempo's malevolent henchmen and navigate treacherous traps set by the dastardly villain.

![Attempo's henchmen]({{ '/' | absolute_url }}/assets/images/projects/77Attempts/77Enemies.jpg){: .align-center}

If you wish to see more gameplay, click [here](https://youtu.be/lv0hjZbbIfA).

## An interesting platform

"77 Attempts" was completely built in Z80 Assembly, targeting the Zilog CPU chip that the Amstrad CPC 464 platform provided. The version we worked on counted with a total of 64KB of RAM and its CPU run at 4 MHz, which made the whole experience even more interesting. Some of the features we were forced to implement due to the nature of the platform were:
- **Double buffer:** Some of our levels require more CPU processing and they couldn't keep up with the 50Hz screen refresh rate of the computer, specifically this was happening in the levels we had multiple enemies refreshing every CPU cycle. This caused artifacting, and to fix it, we implemented [double buffer](https://www.cpcwiki.eu/index.php/Programming_methods_used_in_games#Hardware_Double_Buffer), a very widely used technique in many games of the 80's.
- **Compression:** One of the main objectives of our game was to fit as many maps as possible in 32KBs of space, accounting aswell for the double buffer, music, sprites and the space used for rendering and code, for this reason, compressing everything was pretty much a requirement of 77Attempts codebase.
- [**Self-modifying code**](https://en.wikipedia.org/wiki/Self-modifying_code): With the effort of saving even the last byte of the limited RAM that the CPC counted with, there were efforts on specific parts of the codebase of employing this technique to reduce the memory footprint of specific code sections.

These techniques, to name a few, encompass the low-level mindset that programmers need to have when programming to constrained platforms. I think it was a really interesting exercise to grow into the role of a software engineer.

My main responsibilities in "77 Attempts" were code and gfx, including level design and sound.