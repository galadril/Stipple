# name: YouTube
# summary: Live subscriber count for a channel you pick in the web page, no API key.
# author: Stipple
# tags: http, social, youtube, graph
# panel: 52x16

# @config chan text "Channel ID" default="UCpGLALzRO0uaasWTsm9M99w" maxlen=32 help="The UC... part of youtube.com/channel/UC..., not the @handle"
# @config views boolean "Show total views instead" default=false

import string
import json

# Subscriber count from socialcounts.org, which needs no account and no key -
# which is most of why this endpoint and not YouTube's own Data API, where a
# key would have to be typed into a device and then stored in plaintext on it.
#
# The channel is a setting rather than a constant in the source. Before
# `@config` existed, "use your own channel" meant opening the script, finding
# a string literal and hoping; now the device's web page renders a labelled
# box and this reads it out of the store it was already using. A firmware too
# old to know about settings still runs this, because `store.get` falls back
# on its own.
#
# The reply is about 150 bytes, which matters: a script parses on the frame
# the data lands, and the panel has a budget per frame.

class App
  var URL, chan
  var subs, prev
  var spark        # the last 52 readings, for the trend line
  var at

  def init()
    self.subs = -1
    self.prev = -1
    self.spark = []
    self.at = 0
    var i = 0
    while i < 52
      self.spark.push(-1)
      i += 1
    end
    self._point()
  end

  # Rebuilt when the setting changes rather than every frame: it is string
  # concatenation, and http_follow wants the same URL each time or it would
  # register a new feed on every call.
  def _point()
    self.chan = store.get("chan", "UCpGLALzRO0uaasWTsm9M99w")
    self.URL = "https://api.socialcounts.org/youtube-live-subscriber-count/" + self.chan
  end

  def duration()
    return 8000
  end

  def _parse()
    var body = http_get(self.URL)
    if body == nil
      return
    end
    var doc = json.load(body)
    if doc == nil
      return
    end
    var counters = doc.find("counters")
    if counters == nil
      return
    end
    # `api` is what YouTube last published, `estimation` is their guess
    # between publishes. The published one is the honest number.
    var block = counters.find("api")
    if block == nil
      block = counters.find("estimation")
    end
    if block == nil
      return
    end

    var key = store.get("views", false) ? "viewCount" : "subscriberCount"
    var value = block.find(key)
    if value == nil
      return
    end

    var n = int(value)
    if n < 0
      return
    end
    if n != self.subs
      self.prev = self.subs
      self.subs = n
      self.spark[self.at] = n
      self.at = (self.at + 1) % 52
    end
  end

  # 1234567 -> "1.2M". A count that does not fit is worse than a rounded one:
  # seven digits is forty-two pixels of a fifty-two pixel panel, leaving no
  # room for anything to say what it counts.
  def _short(n)
    if n >= 1000000
      return string.format("%d.%dM", n / 1000000, (n % 1000000) / 100000)
    elif n >= 10000
      return string.format("%dK", n / 1000)
    elif n >= 1000
      return string.format("%d.%dK", n / 1000, (n % 1000) / 100)
    end
    return str(n)
  end

  def draw()
    clear(rgb(0, 0, 0))
    self._point()
    http_follow(self.URL, 120)

    if !http_known()
      text(2, 1, "no net", rgb(150, 60, 60))
      return
    end

    self._parse()

    if self.subs < 0
      var why = http_error(self.URL)
      if why != nil
        text(1, 1, "err", rgb(200, 70, 50))
        text(1, 9, why, rgb(90, 60, 60))
      elif http_status(self.URL) == 404
        # The commonest mistake by a distance, and worth naming: an @handle
        # is not a channel ID and the endpoint 404s on one.
        text(1, 1, "bad id", rgb(200, 70, 50))
        text(1, 9, "use UC..", rgb(90, 60, 60))
      else
        text(1, 5, "fetching", rgb(70, 70, 70))
      end
      return
    end

    # The play button, so it reads as YouTube without a logo nobody licensed.
    rect_fill(0, 3, 11, 8, rgb(190, 30, 25))
    line(4, 5, 4, 8, rgb(255, 255, 255))
    line(5, 6, 5, 7, rgb(255, 255, 255))
    pixel(6, 6, rgb(255, 255, 255))
    pixel(6, 7, rgb(255, 255, 255))

    var label = self._short(self.subs)
    text(14, 0, label, rgb(240, 240, 240))

    var what = store.get("views", false) ? "views" : "subs"
    text(14, 9, what, rgb(70, 76, 90))

    # The trend, as a sparkline across the right-hand columns. Only drawn
    # once there are two readings that differ - a flat line built from one
    # number is a line that says something it does not know.
    self._trend()
  end

  def _trend()
    var low = -1
    var high = -1
    var i = 0
    while i < 52
      var v = self.spark[i]
      if v >= 0
        if low < 0 || v < low low = v end
        if high < 0 || v > high high = v end
      end
      i += 1
    end
    if low < 0 || high <= low
      return
    end

    # Right half only: the number needs the left. Oldest at the left of the
    # strip, which is the direction every chart is read in.
    var x = 0
    while x < 16
      var idx = (self.at + 36 + x) % 52
      var v = self.spark[idx]
      if v >= 0
        var h = ((v - low) * 5) / (high - low)
        rect_fill(36 + x, 13 - h, 1, h + 1, rgb(0, 120, 90))
      end
      x += 1
    end
  end
end

return App()
