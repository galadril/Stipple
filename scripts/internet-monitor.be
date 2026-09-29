# name: Internet Monitor
# summary: Your public IPv4 and whether the internet is actually up, checked against two independent services.
# author: Spectral
# tags: http, network, status, tool
# panel: 52x16

# @config refresh number "Check every (seconds)" default=300 min=60 max=3600 help="How often to ask. Free services are free because nobody hammers them."
# @config fails number "Offline after this many missed checks" default=3 min=1 max=10 help="One failed check is usually a hiccup. This is how many in a row it takes before the panel says OFFLINE."
# @config rainbow boolean "Colour the address" default=true

import string

# Ported from the AWTRIX NG script by Spectral:
# https://git.mike-lindner.net/mike/awtrix-ng-internet-monitor
#
# Same job, different machinery, because almost none of the original's API
# exists here and the differences are worth knowing before you edit this.
#
# **Fetching is a subscription, not a call.** There is no `http.get(url, cb)`
# - it would have to block the thread that draws the panel, and a thirty
# second connect timeout would be thirty seconds of frozen display. A script
# says what it wants and how often; the device fetches on its own schedule and
# the script draws whatever arrived last. So the original's in-flight flag,
# retry timer and attempt counter are all gone: there is nothing to sequence.
#
# **Offline is a duration, not a tally.** The original counted consecutive
# failed attempts because it could see each one. Here the honest equivalent is
# "no good answer for longer than it should have taken", which is the missed
# check count times the interval. It means the same thing and it cannot drift
# out of step with a retry schedule this script does not own.
#
# **No regular expressions.** `re` is not in the sandbox, so the address is
# found by scanning for four groups of digits separated by dots, validating
# each group is 0-255 as it goes. IPv4 only, deliberately, exactly as the
# original said.
#
# **No `hsv()` and no scrolling.** The rainbow is computed here, and the
# address does not scroll because it does not need to: split at the second dot
# it fits on two lines, which can be read at a glance instead of over four
# seconds. The panel is 52 x 16 rather than 32 x 8, and that is most of what
# the extra room is for.
#
# Also gone: the `rotation.pause()` sequence, because a script cannot hold the
# carousel, and the "MY IP" intro, because two lines of address under a tick
# do not need announcing.

class App
  var U1, U2
  var DIGITS
  var refresh, graceMs, rainbow
  var ip

  def init()
    # ipify returns the bare address and nothing else. The AWS endpoint is the
    # second opinion - two services so that "the internet is down" is not
    # actually "one company is down".
    self.U1 = "https://api.ipify.org"
    self.U2 = "https://checkip.amazonaws.com"

    self.DIGITS = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]

    self.refresh = store.get("refresh", 300)
    var n = store.get("fails", 3)
    self.graceMs = self.refresh * n * 1000
    self.rainbow = store.get("rainbow", true)

    # Remembered across a restart, so a device that boots while the line is
    # down can still tell you what the address was.
    self.ip = store.get("ip", nil)
  end

  def duration()
    return 12000
  end

  # --- parsing -------------------------------------------------------------

  def _dig(c)
    var i = 0
    while i < 10
      if c == self.DIGITS[i]
        return i
      end
      i += 1
    end
    return -1
  end

  # The first dotted quad in the body, or nil.
  #
  # Each group is validated as it is read rather than afterwards, so
  # "999.1.1.1" is rejected where a shape-only check would have shown it. The
  # bodies come from services that return nothing but an address, but a script
  # that will happily print any four numbers it finds is one that prints
  # something wrong the first time a service returns an error page.
  def _find(body)
    if body == nil
      return nil
    end
    var n = size(body)
    if n > 256
      n = 256
    end

    var i = 0
    while i < n
      if self._dig(body[i]) >= 0
        var start = i
        var j = i
        var groups = 0
        var ok = true

        while groups < 4
          var digits = 0
          var value = 0
          while j < n && self._dig(body[j]) >= 0
            value = value * 10 + self._dig(body[j])
            digits += 1
            j += 1
          end
          if digits < 1 || digits > 3 || value > 255
            ok = false
            break
          end
          groups += 1
          if groups < 4
            if j < n && body[j] == "."
              j += 1
            else
              ok = false
              break
            end
          end
        end

        if ok && groups == 4
          return body[start .. j - 1]
        end
        i = start + 1
      else
        i += 1
      end
    end
    return nil
  end

  # How long ago each of two different things last happened, in milliseconds,
  # with -1 for never: [an address we could read, a reply of any kind].
  #
  # Two numbers rather than one, because a service that answers with something
  # this cannot parse is not the same as a service that does not answer. The
  # first version returned only the address age and showed an unreadable reply
  # as CHECK - stuck for ever on a panel that had, in fact, just proved the
  # internet works by receiving something over it.
  #
  # Sets self.ip as a side effect, which is what makes the last known address
  # survive the line going down.
  def _ages()
    var bestIp = -1
    var bestAny = -1
    var found = nil

    var i = 0
    var urls = [self.U1, self.U2]
    while i < 2
      var a = http_age_ms(urls[i])
      if a >= 0
        if bestAny < 0 || a < bestAny
          bestAny = a
        end
        var v = self._find(http_get(urls[i]))
        if v != nil && (bestIp < 0 || a < bestIp)
          bestIp = a
          found = v
        end
      end
      i += 1
    end

    if found != nil && found != self.ip
      self.ip = found
      store.set("ip", found)
    end
    return [bestIp, bestAny]
  end

  # --- drawing -------------------------------------------------------------

  def _hue(h)
    var x = h % 360
    var seg = int(x / 60)
    var f = x % 60
    var up = int(f * 255 / 60)
    var dn = 255 - up
    if seg == 0
      return rgb(255, up, 0)
    elif seg == 1
      return rgb(dn, 255, 0)
    elif seg == 2
      return rgb(0, 255, up)
    elif seg == 3
      return rgb(0, dn, 255)
    elif seg == 4
      return rgb(up, 0, 255)
    end
    return rgb(255, 0, dn)
  end

  def _tick(ox, oy, c)
    line(ox, oy + 3, ox + 1, oy + 4, c)
    line(ox + 1, oy + 4, ox + 4, oy + 1, c)
  end

  def _cross(ox, oy, c)
    line(ox, oy, ox + 4, oy + 4, c)
    line(ox, oy + 4, ox + 4, oy, c)
  end

  # "192.168.178.42" -> "192.168." and "178.42".
  #
  # Split after the second dot, which puts the network half on top and the
  # host half underneath - the half that changes is the one on the bottom
  # line, where the eye lands last.
  def _split(ip)
    var seen = 0
    var i = 0
    while i < size(ip)
      if ip[i] == "."
        seen += 1
        if seen == 2
          return [ip[0 .. i], ip[i + 1 .. size(ip) - 1]]
        end
      end
      i += 1
    end
    return [ip, ""]
  end

  def _address(y, plain)
    var parts = self._split(self.ip)
    var top = parts[0]
    var bottom = parts[1]

    if !self.rainbow || plain
      text(0, y, top, rgb(200, 210, 220))
      text(0, y + 8, bottom, rgb(200, 210, 220))
      return
    end

    # A slow sweep along the address rather than a scroll. Same idea as the
    # original's rainbow, without moving the thing you are trying to read.
    #
    # Fourteen degrees a character, not thirty. The first attempt spread the
    # full spectrum across fourteen characters and the address came out as
    # noise - every digit a different colour reads as decoration rather than
    # as a number. A narrow span sweeping slowly is a gradient the eye can
    # follow across something it is still trying to read.
    var phase = int(now_ms() / 70) % 360
    var x = 0
    var i = 0
    while i < size(top)
      x += text(x, y, top[i], self._hue(phase + i * 14))
      i += 1
    end

    x = 0
    var k = 0
    while k < size(bottom)
      x += text(x, y + 8, bottom[k], self._hue(phase + (i + k) * 14))
      k += 1
    end
  end

  def _ago(ms)
    var s = int(ms / 1000)
    if s < 90
      return str(s) + "s"
    end
    var m = int(s / 60)
    if m < 90
      return str(m) + "m"
    end
    return str(int(m / 60)) + "h"
  end

  def draw()
    clear(rgb(0, 0, 0))

    if !http_known()
      # Not the same as being offline, and worth the distinction: this is the
      # device having no network at all rather than the internet being down.
      text(2, 0, "no", rgb(120, 120, 120))
      text(2, 9, "net", rgb(150, 70, 60))
      return
    end

    # Every frame, because there is nowhere else to ask. Asking again is free.
    http_follow(self.U1, self.refresh)

    # The second service is only taken up once the first has let us down.
    # Following both from the start would double the load on two free
    # endpoints to answer a question one of them almost always answers.
    if http_error(self.U1) != nil || http_age_ms(self.U1) > self.graceMs
      http_follow(self.U2, self.refresh)
    end

    var ages = self._ages()
    var age = ages[0]
    var any = ages[1]

    # Never had an answer, and nothing has failed yet: the first fetch is
    # still on its way. Not an error, and not offline.
    if any < 0 && http_error(self.U1) == nil
      text(10, 5, "CHECK", rgb(110, 110, 120))
      return
    end

    # Something replied recently but there was no address in it. The internet
    # is plainly up - a reply came over it - so calling this OFFLINE would be
    # the wrong answer to the question the panel is actually being asked. The
    # service has changed what it returns, and saying so beats blaming the
    # line.
    if (age < 0 || age > self.graceMs) && any >= 0 && any <= self.graceMs
      self._tick(46, 1, rgb(190, 160, 60))
      if self.ip == nil
        text(0, 0, "no addr", rgb(190, 160, 60))
        text(0, 9, "in reply", rgb(110, 95, 60))
      else
        # The last address we did read, dimmed, because it is no longer being
        # confirmed even though the connection is fine.
        self._address(0, true)
      end
      return
    end

    # Offline: no good answer for longer than the missed-check budget allows.
    if age < 0 || age > self.graceMs
      var red = rgb(230, 70, 60)
      self._cross(0, 1, red)
      text(8, 0, "OFFLINE", red)

      if self.ip == nil
        text(8, 9, "no address", rgb(90, 60, 60))
      else
        # How long it has been wrong is more use here than an address that
        # stopped being true at some point in the past.
        var since = "for " + self._ago(age)
        if age < 0
          since = "never up"
        end
        text(8, 9, since, rgb(110, 80, 75))
      end
      return
    end

    # Online, with the address on two lines and a tick that does not move.
    self._tick(46, 1, rgb(80, 200, 100))
    self._address(0, false)
  end
end

return App()
