!(function (e, n) {
    "object" == typeof exports && "undefined" != typeof module
        ? n(exports)
        : "function" == typeof define && define.amd
          ? define(["exports"], n)
          : n(((e = e || self).anne$util = {}));
})(this, function (e) {
    "use strict";
    /*! *****************************************************************************
    Copyright (c) Microsoft Corporation. All rights reserved.
    Licensed under the Apache License, Version 2.0 (the "License"); you may not use
    this file except in compliance with the License. You may obtain a copy of the
    License at http://www.apache.org/licenses/LICENSE-2.0

    THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
    KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
    WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
    MERCHANTABLITY OR NON-INFRINGEMENT.

    See the Apache Version 2.0 License for specific language governing permissions
    and limitations under the License.
    ***************************************************************************** */ function n(e, n) {
        if (!(e instanceof n)) throw new TypeError("Cannot call a class as a function");
    }
    function r(e, n) {
        for (var r = 0; r < n.length; r++) {
            var t = n[r];
            (t.enumerable = t.enumerable || !1),
                (t.configurable = !0),
                "value" in t && (t.writable = !0),
                Object.defineProperty(e, t.key, t);
        }
    }
    function t(e, n, t) {
        return n && r(e.prototype, n), t && r(e, t), e;
    }
    var i = Date.UTC(2014, 7, 11, 2, 0, 0, 0) / 1e3,
        o = (function () {
            function e(r) {
                n(this, e), (this._chainTimestamp = r);
            }
            return (
                t(
                    e,
                    [
                        {
                            key: "getChainTimestamp",
                            value: function () {
                                return this._chainTimestamp;
                            },
                        },
                        {
                            key: "setChainTimestamp",
                            value: function (e) {
                                this._chainTimestamp = e;
                            },
                        },
                        {
                            key: "getEpoch",
                            value: function () {
                                return 1e3 * (i + this._chainTimestamp);
                            },
                        },
                        {
                            key: "getDate",
                            value: function () {
                                return new Date(this.getEpoch());
                            },
                        },
                        {
                            key: "setDate",
                            value: function (e) {
                                this._chainTimestamp = Math.round(e.getTime() / 1e3) - i;
                            },
                        },
                        {
                            key: "equals",
                            value: function (e) {
                                return this._chainTimestamp === e._chainTimestamp;
                            },
                        },
                        {
                            key: "before",
                            value: function (e) {
                                return this._chainTimestamp < e._chainTimestamp;
                            },
                        },
                        {
                            key: "after",
                            value: function (e) {
                                return this._chainTimestamp > e._chainTimestamp;
                            },
                        },
                    ],
                    [
                        {
                            key: "fromChainTimestamp",
                            value: function (n) {
                                return new e(n);
                            },
                        },
                        {
                            key: "fromDate",
                            value: function (n) {
                                var r = new e(0);
                                return r.setDate(n), r;
                            },
                        },
                    ]
                ),
                e
            );
        })(),
        a = /^-?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i,
        c = Math.ceil,
        u = Math.floor,
        l = "[BigNumber Error] ",
        s = l + "Number primitive has more than 15 significant digits: ",
        f = 1e14,
        h = [1, 10, 100, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13],
        g = 1e9;
    function p(e) {
        var n = 0 | e;
        return e > 0 || e === n ? n : n - 1;
    }
    function d(e) {
        for (var n, r, t = 1, i = e.length, o = e[0] + ""; t < i; ) {
            for (r = 14 - (n = e[t++] + "").length; r--; n = "0" + n);
            o += n;
        }
        for (i = o.length; 48 === o.charCodeAt(--i); );
        return o.slice(0, i + 1 || 1);
    }
    function v(e, n) {
        var r,
            t,
            i = e.c,
            o = n.c,
            a = e.s,
            c = n.s,
            u = e.e,
            l = n.e;
        if (!a || !c) return null;
        if (((r = i && !i[0]), (t = o && !o[0]), r || t)) return r ? (t ? 0 : -c) : a;
        if (a != c) return a;
        if (((r = a < 0), (t = u == l), !i || !o)) return t ? 0 : !i ^ r ? 1 : -1;
        if (!t) return (u > l) ^ r ? 1 : -1;
        for (c = (u = i.length) < (l = o.length) ? u : l, a = 0; a < c; a++)
            if (i[a] != o[a]) return (i[a] > o[a]) ^ r ? 1 : -1;
        return u == l ? 0 : (u > l) ^ r ? 1 : -1;
    }
    function m(e, n, r, t) {
        if (e < n || e > r || e !== u(e))
            throw Error(
                l +
                    (t || "Argument") +
                    ("number" == typeof e
                        ? e < n || e > r
                            ? " out of range: "
                            : " not an integer: "
                        : " not a primitive number: ") +
                    String(e)
            );
    }
    function y(e) {
        var n = e.c.length - 1;
        return p(e.e / 14) == n && e.c[n] % 2 != 0;
    }
    function w(e, n) {
        return (e.length > 1 ? e.charAt(0) + "." + e.slice(1) : e) + (n < 0 ? "e" : "e+") + n;
    }
    function S(e, n, r) {
        var t, i;
        if (n < 0) {
            for (i = r + "."; ++n; i += r);
            e = i + e;
        } else if (++n > (t = e.length)) {
            for (i = r, n -= t; --n; i += r);
            e += i;
        } else n < t && (e = e.slice(0, n) + "." + e.slice(n));
        return e;
    }
    var A,
        b = (function e(n) {
            var r,
                t,
                i,
                o,
                A,
                b,
                E,
                k,
                N,
                x = (M.prototype = { constructor: M, toString: null, valueOf: null }),
                T = new M(1),
                _ = 20,
                O = 4,
                C = -7,
                B = 21,
                F = -1e7,
                D = 1e7,
                P = !1,
                I = 1,
                U = 0,
                R = {
                    prefix: "",
                    groupSize: 3,
                    secondaryGroupSize: 0,
                    groupSeparator: ",",
                    decimalSeparator: ".",
                    fractionGroupSize: 0,
                    fractionGroupSeparator: " ",
                    suffix: "",
                },
                L = "0123456789abcdefghijklmnopqrstuvwxyz";
            function M(e, n) {
                var r,
                    o,
                    c,
                    l,
                    f,
                    h,
                    g,
                    p,
                    d = this;
                if (!(d instanceof M)) return new M(e, n);
                if (null == n) {
                    if (e && !0 === e._isBigNumber)
                        return (
                            (d.s = e.s),
                            void (!e.c || e.e > D
                                ? (d.c = d.e = null)
                                : e.e < F
                                  ? (d.c = [(d.e = 0)])
                                  : ((d.e = e.e), (d.c = e.c.slice())))
                        );
                    if ((h = "number" == typeof e) && 0 * e == 0) {
                        if (((d.s = 1 / e < 0 ? ((e = -e), -1) : 1), e === ~~e)) {
                            for (l = 0, f = e; f >= 10; f /= 10, l++);
                            return void (l > D ? (d.c = d.e = null) : ((d.e = l), (d.c = [e])));
                        }
                        p = String(e);
                    } else {
                        if (!a.test((p = String(e)))) return i(d, p, h);
                        d.s = 45 == p.charCodeAt(0) ? ((p = p.slice(1)), -1) : 1;
                    }
                    (l = p.indexOf(".")) > -1 && (p = p.replace(".", "")),
                        (f = p.search(/e/i)) > 0
                            ? (l < 0 && (l = f), (l += +p.slice(f + 1)), (p = p.substring(0, f)))
                            : l < 0 && (l = p.length);
                } else {
                    if ((m(n, 2, L.length, "Base"), 10 == n)) return q((d = new M(e)), _ + d.e + 1, O);
                    if (((p = String(e)), (h = "number" == typeof e))) {
                        if (0 * e != 0) return i(d, p, h, n);
                        if (
                            ((d.s = 1 / e < 0 ? ((p = p.slice(1)), -1) : 1),
                            M.DEBUG && p.replace(/^0\.0*|\./, "").length > 15)
                        )
                            throw Error(s + e);
                    } else d.s = 45 === p.charCodeAt(0) ? ((p = p.slice(1)), -1) : 1;
                    for (r = L.slice(0, n), l = f = 0, g = p.length; f < g; f++)
                        if (r.indexOf((o = p.charAt(f))) < 0) {
                            if ("." == o) {
                                if (f > l) {
                                    l = g;
                                    continue;
                                }
                            } else if (
                                !c &&
                                ((p == p.toUpperCase() && (p = p.toLowerCase())) ||
                                    (p == p.toLowerCase() && (p = p.toUpperCase())))
                            ) {
                                (c = !0), (f = -1), (l = 0);
                                continue;
                            }
                            return i(d, String(e), h, n);
                        }
                    (h = !1),
                        (l = (p = t(p, n, 10, d.s)).indexOf(".")) > -1 ? (p = p.replace(".", "")) : (l = p.length);
                }
                for (f = 0; 48 === p.charCodeAt(f); f++);
                for (g = p.length; 48 === p.charCodeAt(--g); );
                if ((p = p.slice(f, ++g))) {
                    if (((g -= f), h && M.DEBUG && g > 15 && (e > 9007199254740991 || e !== u(e))))
                        throw Error(s + d.s * e);
                    if ((l = l - f - 1) > D) d.c = d.e = null;
                    else if (l < F) d.c = [(d.e = 0)];
                    else {
                        if (((d.e = l), (d.c = []), (f = (l + 1) % 14), l < 0 && (f += 14), f < g)) {
                            for (f && d.c.push(+p.slice(0, f)), g -= 14; f < g; ) d.c.push(+p.slice(f, (f += 14)));
                            f = 14 - (p = p.slice(f)).length;
                        } else f -= g;
                        for (; f--; p += "0");
                        d.c.push(+p);
                    }
                } else d.c = [(d.e = 0)];
            }
            function H(e, n, r, t) {
                var i, o, a, c, u;
                if ((null == r ? (r = O) : m(r, 0, 8), !e.c)) return e.toString();
                if (((i = e.c[0]), (a = e.e), null == n))
                    (u = d(e.c)), (u = 1 == t || (2 == t && (a <= C || a >= B)) ? w(u, a) : S(u, a, "0"));
                else if (
                    ((o = (e = q(new M(e), n, r)).e),
                    (c = (u = d(e.c)).length),
                    1 == t || (2 == t && (n <= o || o <= C)))
                ) {
                    for (; c < n; u += "0", c++);
                    u = w(u, o);
                } else if (((n -= a), (u = S(u, o, "0")), o + 1 > c)) {
                    if (--n > 0) for (u += "."; n--; u += "0");
                } else if ((n += o - c) > 0) for (o + 1 == c && (u += "."); n--; u += "0");
                return e.s < 0 && i ? "-" + u : u;
            }
            function G(e, n) {
                for (var r, t = 1, i = new M(e[0]); t < e.length; t++) {
                    if (!(r = new M(e[t])).s) {
                        i = r;
                        break;
                    }
                    n.call(i, r) && (i = r);
                }
                return i;
            }
            function j(e, n, r) {
                for (var t = 1, i = n.length; !n[--i]; n.pop());
                for (i = n[0]; i >= 10; i /= 10, t++);
                return (
                    (r = t + 14 * r - 1) > D
                        ? (e.c = e.e = null)
                        : r < F
                          ? (e.c = [(e.e = 0)])
                          : ((e.e = r), (e.c = n)),
                    e
                );
            }
            function q(e, n, r, t) {
                var i,
                    o,
                    a,
                    l,
                    s,
                    g,
                    p,
                    d = e.c,
                    v = h;
                if (d) {
                    e: {
                        for (i = 1, l = d[0]; l >= 10; l /= 10, i++);
                        if ((o = n - i) < 0) (o += 14), (a = n), (p = ((s = d[(g = 0)]) / v[i - a - 1]) % 10 | 0);
                        else if ((g = c((o + 1) / 14)) >= d.length) {
                            if (!t) break e;
                            for (; d.length <= g; d.push(0));
                            (s = p = 0), (i = 1), (a = (o %= 14) - 14 + 1);
                        } else {
                            for (s = l = d[g], i = 1; l >= 10; l /= 10, i++);
                            p = (a = (o %= 14) - 14 + i) < 0 ? 0 : (s / v[i - a - 1]) % 10 | 0;
                        }
                        if (
                            ((t = t || n < 0 || null != d[g + 1] || (a < 0 ? s : s % v[i - a - 1])),
                            (t =
                                r < 4
                                    ? (p || t) && (0 == r || r == (e.s < 0 ? 3 : 2))
                                    : p > 5 ||
                                      (5 == p &&
                                          (4 == r ||
                                              t ||
                                              (6 == r && (o > 0 ? (a > 0 ? s / v[i - a] : 0) : d[g - 1]) % 10 & 1) ||
                                              r == (e.s < 0 ? 8 : 7)))),
                            n < 1 || !d[0])
                        )
                            return (
                                (d.length = 0),
                                t
                                    ? ((n -= e.e + 1), (d[0] = v[(14 - (n % 14)) % 14]), (e.e = -n || 0))
                                    : (d[0] = e.e = 0),
                                e
                            );
                        if (
                            (0 == o
                                ? ((d.length = g), (l = 1), g--)
                                : ((d.length = g + 1),
                                  (l = v[14 - o]),
                                  (d[g] = a > 0 ? u((s / v[i - a]) % v[a]) * l : 0)),
                            t)
                        )
                            for (;;) {
                                if (0 == g) {
                                    for (o = 1, a = d[0]; a >= 10; a /= 10, o++);
                                    for (a = d[0] += l, l = 1; a >= 10; a /= 10, l++);
                                    o != l && (e.e++, d[0] == f && (d[0] = 1));
                                    break;
                                }
                                if (((d[g] += l), d[g] != f)) break;
                                (d[g--] = 0), (l = 1);
                            }
                        for (o = d.length; 0 === d[--o]; d.pop());
                    }
                    e.e > D ? (e.c = e.e = null) : e.e < F && (e.c = [(e.e = 0)]);
                }
                return e;
            }
            function z(e) {
                var n,
                    r = e.e;
                return null === r
                    ? e.toString()
                    : ((n = d(e.c)), (n = r <= C || r >= B ? w(n, r) : S(n, r, "0")), e.s < 0 ? "-" + n : n);
            }
            return (
                (M.clone = e),
                (M.ROUND_UP = 0),
                (M.ROUND_DOWN = 1),
                (M.ROUND_CEIL = 2),
                (M.ROUND_FLOOR = 3),
                (M.ROUND_HALF_UP = 4),
                (M.ROUND_HALF_DOWN = 5),
                (M.ROUND_HALF_EVEN = 6),
                (M.ROUND_HALF_CEIL = 7),
                (M.ROUND_HALF_FLOOR = 8),
                (M.EUCLID = 9),
                (M.config = M.set =
                    function (e) {
                        var n, r;
                        if (null != e) {
                            if ("object" != typeof e) throw Error(l + "Object expected: " + e);
                            if (
                                (e.hasOwnProperty((n = "DECIMAL_PLACES")) && (m((r = e[n]), 0, g, n), (_ = r)),
                                e.hasOwnProperty((n = "ROUNDING_MODE")) && (m((r = e[n]), 0, 8, n), (O = r)),
                                e.hasOwnProperty((n = "EXPONENTIAL_AT")) &&
                                    ((r = e[n]) && r.pop
                                        ? (m(r[0], -g, 0, n), m(r[1], 0, g, n), (C = r[0]), (B = r[1]))
                                        : (m(r, -g, g, n), (C = -(B = r < 0 ? -r : r)))),
                                e.hasOwnProperty((n = "RANGE")))
                            )
                                if ((r = e[n]) && r.pop) m(r[0], -g, -1, n), m(r[1], 1, g, n), (F = r[0]), (D = r[1]);
                                else {
                                    if ((m(r, -g, g, n), !r)) throw Error(l + n + " cannot be zero: " + r);
                                    F = -(D = r < 0 ? -r : r);
                                }
                            if (e.hasOwnProperty((n = "CRYPTO"))) {
                                if ((r = e[n]) !== !!r) throw Error(l + n + " not true or false: " + r);
                                if (r) {
                                    if (
                                        "undefined" == typeof crypto ||
                                        !crypto ||
                                        (!crypto.getRandomValues && !crypto.randomBytes)
                                    )
                                        throw ((P = !r), Error(l + "crypto unavailable"));
                                    P = r;
                                } else P = r;
                            }
                            if (
                                (e.hasOwnProperty((n = "MODULO_MODE")) && (m((r = e[n]), 0, 9, n), (I = r)),
                                e.hasOwnProperty((n = "POW_PRECISION")) && (m((r = e[n]), 0, g, n), (U = r)),
                                e.hasOwnProperty((n = "FORMAT")))
                            ) {
                                if ("object" != typeof (r = e[n])) throw Error(l + n + " not an object: " + r);
                                R = r;
                            }
                            if (e.hasOwnProperty((n = "ALPHABET"))) {
                                if ("string" != typeof (r = e[n]) || /^.$|[+-.\s]|(.).*\1/.test(r))
                                    throw Error(l + n + " invalid: " + r);
                                L = r;
                            }
                        }
                        return {
                            DECIMAL_PLACES: _,
                            ROUNDING_MODE: O,
                            EXPONENTIAL_AT: [C, B],
                            RANGE: [F, D],
                            CRYPTO: P,
                            MODULO_MODE: I,
                            POW_PRECISION: U,
                            FORMAT: R,
                            ALPHABET: L,
                        };
                    }),
                (M.isBigNumber = function (e) {
                    if (!e || !0 !== e._isBigNumber) return !1;
                    if (!M.DEBUG) return !0;
                    var n,
                        r,
                        t = e.c,
                        i = e.e,
                        o = e.s;
                    e: if ("[object Array]" == {}.toString.call(t)) {
                        if ((1 === o || -1 === o) && i >= -g && i <= g && i === u(i)) {
                            if (0 === t[0]) {
                                if (0 === i && 1 === t.length) return !0;
                                break e;
                            }
                            if (((n = (i + 1) % 14) < 1 && (n += 14), String(t[0]).length == n)) {
                                for (n = 0; n < t.length; n++) if ((r = t[n]) < 0 || r >= f || r !== u(r)) break e;
                                if (0 !== r) return !0;
                            }
                        }
                    } else if (null === t && null === i && (null === o || 1 === o || -1 === o)) return !0;
                    throw Error(l + "Invalid BigNumber: " + e);
                }),
                (M.maximum = M.max =
                    function () {
                        return G(arguments, x.lt);
                    }),
                (M.minimum = M.min =
                    function () {
                        return G(arguments, x.gt);
                    }),
                (M.random =
                    ((o =
                        (9007199254740992 * Math.random()) & 2097151
                            ? function () {
                                  return u(9007199254740992 * Math.random());
                              }
                            : function () {
                                  return 8388608 * ((1073741824 * Math.random()) | 0) + ((8388608 * Math.random()) | 0);
                              }),
                    function (e) {
                        var n,
                            r,
                            t,
                            i,
                            a,
                            s = 0,
                            f = [],
                            p = new M(T);
                        if ((null == e ? (e = _) : m(e, 0, g), (i = c(e / 14)), P))
                            if (crypto.getRandomValues) {
                                for (n = crypto.getRandomValues(new Uint32Array((i *= 2))); s < i; )
                                    (a = 131072 * n[s] + (n[s + 1] >>> 11)) >= 9e15
                                        ? ((r = crypto.getRandomValues(new Uint32Array(2))),
                                          (n[s] = r[0]),
                                          (n[s + 1] = r[1]))
                                        : (f.push(a % 1e14), (s += 2));
                                s = i / 2;
                            } else {
                                if (!crypto.randomBytes) throw ((P = !1), Error(l + "crypto unavailable"));
                                for (n = crypto.randomBytes((i *= 7)); s < i; )
                                    (a =
                                        281474976710656 * (31 & n[s]) +
                                        1099511627776 * n[s + 1] +
                                        4294967296 * n[s + 2] +
                                        16777216 * n[s + 3] +
                                        (n[s + 4] << 16) +
                                        (n[s + 5] << 8) +
                                        n[s + 6]) >= 9e15
                                        ? crypto.randomBytes(7).copy(n, s)
                                        : (f.push(a % 1e14), (s += 7));
                                s = i / 7;
                            }
                        if (!P) for (; s < i; ) (a = o()) < 9e15 && (f[s++] = a % 1e14);
                        for (
                            e %= 14, (i = f[--s]) && e && ((a = h[14 - e]), (f[s] = u(i / a) * a));
                            0 === f[s];
                            f.pop(), s--
                        );
                        if (s < 0) f = [(t = 0)];
                        else {
                            for (t = -1; 0 === f[0]; f.splice(0, 1), t -= 14);
                            for (s = 1, a = f[0]; a >= 10; a /= 10, s++);
                            s < 14 && (t -= 14 - s);
                        }
                        return (p.e = t), (p.c = f), p;
                    })),
                (M.sum = function () {
                    for (var e = 1, n = arguments, r = new M(n[0]); e < n.length; ) r = r.plus(n[e++]);
                    return r;
                }),
                (t = (function () {
                    function e(e, n, r, t) {
                        for (var i, o, a = [0], c = 0, u = e.length; c < u; ) {
                            for (o = a.length; o--; a[o] *= n);
                            for (a[0] += t.indexOf(e.charAt(c++)), i = 0; i < a.length; i++)
                                a[i] > r - 1 &&
                                    (null == a[i + 1] && (a[i + 1] = 0), (a[i + 1] += (a[i] / r) | 0), (a[i] %= r));
                        }
                        return a.reverse();
                    }
                    return function (n, t, i, o, a) {
                        var c,
                            u,
                            l,
                            s,
                            f,
                            h,
                            g,
                            p,
                            v = n.indexOf("."),
                            m = _,
                            y = O;
                        for (
                            v >= 0 &&
                                ((s = U),
                                (U = 0),
                                (n = n.replace(".", "")),
                                (h = (p = new M(t)).pow(n.length - v)),
                                (U = s),
                                (p.c = e(S(d(h.c), h.e, "0"), 10, i, "0123456789")),
                                (p.e = p.c.length)),
                                l = s = (g = e(n, t, i, a ? ((c = L), "0123456789") : ((c = "0123456789"), L))).length;
                            0 == g[--s];
                            g.pop()
                        );
                        if (!g[0]) return c.charAt(0);
                        if (
                            (v < 0
                                ? --l
                                : ((h.c = g),
                                  (h.e = l),
                                  (h.s = o),
                                  (g = (h = r(h, p, m, y, i)).c),
                                  (f = h.r),
                                  (l = h.e)),
                            (v = g[(u = l + m + 1)]),
                            (s = i / 2),
                            (f = f || u < 0 || null != g[u + 1]),
                            (f =
                                y < 4
                                    ? (null != v || f) && (0 == y || y == (h.s < 0 ? 3 : 2))
                                    : v > s ||
                                      (v == s && (4 == y || f || (6 == y && 1 & g[u - 1]) || y == (h.s < 0 ? 8 : 7)))),
                            u < 1 || !g[0])
                        )
                            n = f ? S(c.charAt(1), -m, c.charAt(0)) : c.charAt(0);
                        else {
                            if (((g.length = u), f))
                                for (--i; ++g[--u] > i; ) (g[u] = 0), u || (++l, (g = [1].concat(g)));
                            for (s = g.length; !g[--s]; );
                            for (v = 0, n = ""; v <= s; n += c.charAt(g[v++]));
                            n = S(n, l, c.charAt(0));
                        }
                        return n;
                    };
                })()),
                (r = (function () {
                    function e(e, n, r) {
                        var t,
                            i,
                            o,
                            a,
                            c = 0,
                            u = e.length,
                            l = n % 1e7,
                            s = (n / 1e7) | 0;
                        for (e = e.slice(); u--; )
                            (c =
                                (((i =
                                    l * (o = e[u] % 1e7) + ((t = s * o + (a = (e[u] / 1e7) | 0) * l) % 1e7) * 1e7 + c) /
                                    r) |
                                    0) +
                                ((t / 1e7) | 0) +
                                s * a),
                                (e[u] = i % r);
                        return c && (e = [c].concat(e)), e;
                    }
                    function n(e, n, r, t) {
                        var i, o;
                        if (r != t) o = r > t ? 1 : -1;
                        else
                            for (i = o = 0; i < r; i++)
                                if (e[i] != n[i]) {
                                    o = e[i] > n[i] ? 1 : -1;
                                    break;
                                }
                        return o;
                    }
                    function r(e, n, r, t) {
                        for (var i = 0; r--; ) (e[r] -= i), (i = e[r] < n[r] ? 1 : 0), (e[r] = i * t + e[r] - n[r]);
                        for (; !e[0] && e.length > 1; e.splice(0, 1));
                    }
                    return function (t, i, o, a, c) {
                        var l,
                            s,
                            h,
                            g,
                            d,
                            v,
                            m,
                            y,
                            w,
                            S,
                            A,
                            b,
                            E,
                            k,
                            N,
                            x,
                            T,
                            _ = t.s == i.s ? 1 : -1,
                            O = t.c,
                            C = i.c;
                        if (!(O && O[0] && C && C[0]))
                            return new M(
                                t.s && i.s && (O ? !C || O[0] != C[0] : C)
                                    ? (O && 0 == O[0]) || !C
                                        ? 0 * _
                                        : _ / 0
                                    : NaN
                            );
                        for (
                            w = (y = new M(_)).c = [],
                                _ = o + (s = t.e - i.e) + 1,
                                c || ((c = f), (s = p(t.e / 14) - p(i.e / 14)), (_ = (_ / 14) | 0)),
                                h = 0;
                            C[h] == (O[h] || 0);
                            h++
                        );
                        if ((C[h] > (O[h] || 0) && s--, _ < 0)) w.push(1), (g = !0);
                        else {
                            for (
                                k = O.length,
                                    x = C.length,
                                    h = 0,
                                    _ += 2,
                                    (d = u(c / (C[0] + 1))) > 1 &&
                                        ((C = e(C, d, c)), (O = e(O, d, c)), (x = C.length), (k = O.length)),
                                    E = x,
                                    A = (S = O.slice(0, x)).length;
                                A < x;
                                S[A++] = 0
                            );
                            (T = C.slice()), (T = [0].concat(T)), (N = C[0]), C[1] >= c / 2 && N++;
                            do {
                                if (((d = 0), (l = n(C, S, x, A)) < 0)) {
                                    if (((b = S[0]), x != A && (b = b * c + (S[1] || 0)), (d = u(b / N)) > 1))
                                        for (
                                            d >= c && (d = c - 1), m = (v = e(C, d, c)).length, A = S.length;
                                            1 == n(v, S, m, A);

                                        )
                                            d--, r(v, x < m ? T : C, m, c), (m = v.length), (l = 1);
                                    else 0 == d && (l = d = 1), (m = (v = C.slice()).length);
                                    if ((m < A && (v = [0].concat(v)), r(S, v, A, c), (A = S.length), -1 == l))
                                        for (; n(C, S, x, A) < 1; ) d++, r(S, x < A ? T : C, A, c), (A = S.length);
                                } else 0 === l && (d++, (S = [0]));
                                (w[h++] = d), S[0] ? (S[A++] = O[E] || 0) : ((S = [O[E]]), (A = 1));
                            } while ((E++ < k || null != S[0]) && _--);
                            (g = null != S[0]), w[0] || w.splice(0, 1);
                        }
                        if (c == f) {
                            for (h = 1, _ = w[0]; _ >= 10; _ /= 10, h++);
                            q(y, o + (y.e = h + 14 * s - 1) + 1, a, g);
                        } else (y.e = s), (y.r = +g);
                        return y;
                    };
                })()),
                (A = /^(-?)0([xbo])(?=\w[\w.]*$)/i),
                (b = /^([^.]+)\.$/),
                (E = /^\.([^.]+)$/),
                (k = /^-?(Infinity|NaN)$/),
                (N = /^\s*\+(?=[\w.])|^\s+|\s+$/g),
                (i = function (e, n, r, t) {
                    var i,
                        o = r ? n : n.replace(N, "");
                    if (k.test(o)) e.s = isNaN(o) ? null : o < 0 ? -1 : 1;
                    else {
                        if (
                            !r &&
                            ((o = o.replace(A, function (e, n, r) {
                                return (i = "x" == (r = r.toLowerCase()) ? 16 : "b" == r ? 2 : 8), t && t != i ? e : n;
                            })),
                            t && ((i = t), (o = o.replace(b, "$1").replace(E, "0.$1"))),
                            n != o)
                        )
                            return new M(o, i);
                        if (M.DEBUG) throw Error(l + "Not a" + (t ? " base " + t : "") + " number: " + n);
                        e.s = null;
                    }
                    e.c = e.e = null;
                }),
                (x.absoluteValue = x.abs =
                    function () {
                        var e = new M(this);
                        return e.s < 0 && (e.s = 1), e;
                    }),
                (x.comparedTo = function (e, n) {
                    return v(this, new M(e, n));
                }),
                (x.decimalPlaces = x.dp =
                    function (e, n) {
                        var r,
                            t,
                            i,
                            o = this;
                        if (null != e) return m(e, 0, g), null == n ? (n = O) : m(n, 0, 8), q(new M(o), e + o.e + 1, n);
                        if (!(r = o.c)) return null;
                        if (((t = 14 * ((i = r.length - 1) - p(this.e / 14))), (i = r[i])))
                            for (; i % 10 == 0; i /= 10, t--);
                        return t < 0 && (t = 0), t;
                    }),
                (x.dividedBy = x.div =
                    function (e, n) {
                        return r(this, new M(e, n), _, O);
                    }),
                (x.dividedToIntegerBy = x.idiv =
                    function (e, n) {
                        return r(this, new M(e, n), 0, 1);
                    }),
                (x.exponentiatedBy = x.pow =
                    function (e, n) {
                        var r,
                            t,
                            i,
                            o,
                            a,
                            s,
                            f,
                            h,
                            g = this;
                        if ((e = new M(e)).c && !e.isInteger()) throw Error(l + "Exponent not an integer: " + z(e));
                        if (
                            (null != n && (n = new M(n)),
                            (a = e.e > 14),
                            !g.c || !g.c[0] || (1 == g.c[0] && !g.e && 1 == g.c.length) || !e.c || !e.c[0])
                        )
                            return (h = new M(Math.pow(+z(g), a ? 2 - y(e) : +z(e)))), n ? h.mod(n) : h;
                        if (((s = e.s < 0), n)) {
                            if (n.c ? !n.c[0] : !n.s) return new M(NaN);
                            (t = !s && g.isInteger() && n.isInteger()) && (g = g.mod(n));
                        } else {
                            if (
                                e.e > 9 &&
                                (g.e > 0 ||
                                    g.e < -1 ||
                                    (0 == g.e
                                        ? g.c[0] > 1 || (a && g.c[1] >= 24e7)
                                        : g.c[0] < 8e13 || (a && g.c[0] <= 9999975e7)))
                            )
                                return (o = g.s < 0 && y(e) ? -0 : 0), g.e > -1 && (o = 1 / o), new M(s ? 1 / o : o);
                            U && (o = c(U / 14 + 2));
                        }
                        for (
                            a ? ((r = new M(0.5)), s && (e.s = 1), (f = y(e))) : (f = (i = Math.abs(+z(e))) % 2),
                                h = new M(T);
                            ;

                        ) {
                            if (f) {
                                if (!(h = h.times(g)).c) break;
                                o ? h.c.length > o && (h.c.length = o) : t && (h = h.mod(n));
                            }
                            if (i) {
                                if (0 === (i = u(i / 2))) break;
                                f = i % 2;
                            } else if ((q((e = e.times(r)), e.e + 1, 1), e.e > 14)) f = y(e);
                            else {
                                if (0 === (i = +z(e))) break;
                                f = i % 2;
                            }
                            (g = g.times(g)), o ? g.c && g.c.length > o && (g.c.length = o) : t && (g = g.mod(n));
                        }
                        return t ? h : (s && (h = T.div(h)), n ? h.mod(n) : o ? q(h, U, O, void 0) : h);
                    }),
                (x.integerValue = function (e) {
                    var n = new M(this);
                    return null == e ? (e = O) : m(e, 0, 8), q(n, n.e + 1, e);
                }),
                (x.isEqualTo = x.eq =
                    function (e, n) {
                        return 0 === v(this, new M(e, n));
                    }),
                (x.isFinite = function () {
                    return !!this.c;
                }),
                (x.isGreaterThan = x.gt =
                    function (e, n) {
                        return v(this, new M(e, n)) > 0;
                    }),
                (x.isGreaterThanOrEqualTo = x.gte =
                    function (e, n) {
                        return 1 === (n = v(this, new M(e, n))) || 0 === n;
                    }),
                (x.isInteger = function () {
                    return !!this.c && p(this.e / 14) > this.c.length - 2;
                }),
                (x.isLessThan = x.lt =
                    function (e, n) {
                        return v(this, new M(e, n)) < 0;
                    }),
                (x.isLessThanOrEqualTo = x.lte =
                    function (e, n) {
                        return -1 === (n = v(this, new M(e, n))) || 0 === n;
                    }),
                (x.isNaN = function () {
                    return !this.s;
                }),
                (x.isNegative = function () {
                    return this.s < 0;
                }),
                (x.isPositive = function () {
                    return this.s > 0;
                }),
                (x.isZero = function () {
                    return !!this.c && 0 == this.c[0];
                }),
                (x.minus = function (e, n) {
                    var r,
                        t,
                        i,
                        o,
                        a = this,
                        c = a.s;
                    if (((n = (e = new M(e, n)).s), !c || !n)) return new M(NaN);
                    if (c != n) return (e.s = -n), a.plus(e);
                    var u = a.e / 14,
                        l = e.e / 14,
                        s = a.c,
                        h = e.c;
                    if (!u || !l) {
                        if (!s || !h) return s ? ((e.s = -n), e) : new M(h ? a : NaN);
                        if (!s[0] || !h[0]) return h[0] ? ((e.s = -n), e) : new M(s[0] ? a : 3 == O ? -0 : 0);
                    }
                    if (((u = p(u)), (l = p(l)), (s = s.slice()), (c = u - l))) {
                        for (
                            (o = c < 0) ? ((c = -c), (i = s)) : ((l = u), (i = h)), i.reverse(), n = c;
                            n--;
                            i.push(0)
                        );
                        i.reverse();
                    } else
                        for (t = (o = (c = s.length) < (n = h.length)) ? c : n, c = n = 0; n < t; n++)
                            if (s[n] != h[n]) {
                                o = s[n] < h[n];
                                break;
                            }
                    if ((o && ((i = s), (s = h), (h = i), (e.s = -e.s)), (n = (t = h.length) - (r = s.length)) > 0))
                        for (; n--; s[r++] = 0);
                    for (n = f - 1; t > c; ) {
                        if (s[--t] < h[t]) {
                            for (r = t; r && !s[--r]; s[r] = n);
                            --s[r], (s[t] += f);
                        }
                        s[t] -= h[t];
                    }
                    for (; 0 == s[0]; s.splice(0, 1), --l);
                    return s[0] ? j(e, s, l) : ((e.s = 3 == O ? -1 : 1), (e.c = [(e.e = 0)]), e);
                }),
                (x.modulo = x.mod =
                    function (e, n) {
                        var t,
                            i,
                            o = this;
                        return (
                            (e = new M(e, n)),
                            !o.c || !e.s || (e.c && !e.c[0])
                                ? new M(NaN)
                                : !e.c || (o.c && !o.c[0])
                                  ? new M(o)
                                  : (9 == I
                                        ? ((i = e.s), (e.s = 1), (t = r(o, e, 0, 3)), (e.s = i), (t.s *= i))
                                        : (t = r(o, e, 0, I)),
                                    (e = o.minus(t.times(e))).c[0] || 1 != I || (e.s = o.s),
                                    e)
                        );
                    }),
                (x.multipliedBy = x.times =
                    function (e, n) {
                        var r,
                            t,
                            i,
                            o,
                            a,
                            c,
                            u,
                            l,
                            s,
                            h,
                            g,
                            d,
                            v,
                            m,
                            y = this,
                            w = y.c,
                            S = (e = new M(e, n)).c;
                        if (!(w && S && w[0] && S[0]))
                            return (
                                !y.s || !e.s || (w && !w[0] && !S) || (S && !S[0] && !w)
                                    ? (e.c = e.e = e.s = null)
                                    : ((e.s *= y.s), w && S ? ((e.c = [0]), (e.e = 0)) : (e.c = e.e = null)),
                                e
                            );
                        for (
                            t = p(y.e / 14) + p(e.e / 14),
                                e.s *= y.s,
                                (u = w.length) < (h = S.length) &&
                                    ((v = w), (w = S), (S = v), (i = u), (u = h), (h = i)),
                                i = u + h,
                                v = [];
                            i--;
                            v.push(0)
                        );
                        for (m = f, 1e7, i = h; --i >= 0; ) {
                            for (r = 0, g = S[i] % 1e7, d = (S[i] / 1e7) | 0, o = i + (a = u); o > i; )
                                (r =
                                    (((l =
                                        g * (l = w[--a] % 1e7) +
                                        ((c = d * l + (s = (w[a] / 1e7) | 0) * g) % 1e7) * 1e7 +
                                        v[o] +
                                        r) /
                                        m) |
                                        0) +
                                    ((c / 1e7) | 0) +
                                    d * s),
                                    (v[o--] = l % m);
                            v[o] = r;
                        }
                        return r ? ++t : v.splice(0, 1), j(e, v, t);
                    }),
                (x.negated = function () {
                    var e = new M(this);
                    return (e.s = -e.s || null), e;
                }),
                (x.plus = function (e, n) {
                    var r,
                        t = this,
                        i = t.s;
                    if (((n = (e = new M(e, n)).s), !i || !n)) return new M(NaN);
                    if (i != n) return (e.s = -n), t.minus(e);
                    var o = t.e / 14,
                        a = e.e / 14,
                        c = t.c,
                        u = e.c;
                    if (!o || !a) {
                        if (!c || !u) return new M(i / 0);
                        if (!c[0] || !u[0]) return u[0] ? e : new M(c[0] ? t : 0 * i);
                    }
                    if (((o = p(o)), (a = p(a)), (c = c.slice()), (i = o - a))) {
                        for (i > 0 ? ((a = o), (r = u)) : ((i = -i), (r = c)), r.reverse(); i--; r.push(0));
                        r.reverse();
                    }
                    for ((i = c.length) - (n = u.length) < 0 && ((r = u), (u = c), (c = r), (n = i)), i = 0; n; )
                        (i = ((c[--n] = c[n] + u[n] + i) / f) | 0), (c[n] = f === c[n] ? 0 : c[n] % f);
                    return i && ((c = [i].concat(c)), ++a), j(e, c, a);
                }),
                (x.precision = x.sd =
                    function (e, n) {
                        var r,
                            t,
                            i,
                            o = this;
                        if (null != e && e !== !!e)
                            return m(e, 1, g), null == n ? (n = O) : m(n, 0, 8), q(new M(o), e, n);
                        if (!(r = o.c)) return null;
                        if (((t = 14 * (i = r.length - 1) + 1), (i = r[i]))) {
                            for (; i % 10 == 0; i /= 10, t--);
                            for (i = r[0]; i >= 10; i /= 10, t++);
                        }
                        return e && o.e + 1 > t && (t = o.e + 1), t;
                    }),
                (x.shiftedBy = function (e) {
                    return m(e, -9007199254740991, 9007199254740991), this.times("1e" + e);
                }),
                (x.squareRoot = x.sqrt =
                    function () {
                        var e,
                            n,
                            t,
                            i,
                            o,
                            a = this,
                            c = a.c,
                            u = a.s,
                            l = a.e,
                            s = _ + 4,
                            f = new M("0.5");
                        if (1 !== u || !c || !c[0]) return new M(!u || (u < 0 && (!c || c[0])) ? NaN : c ? a : 1 / 0);
                        if (
                            (0 == (u = Math.sqrt(+z(a))) || u == 1 / 0
                                ? (((n = d(c)).length + l) % 2 == 0 && (n += "0"),
                                  (u = Math.sqrt(+n)),
                                  (l = p((l + 1) / 2) - (l < 0 || l % 2)),
                                  (t = new M(
                                      (n =
                                          u == 1 / 0
                                              ? "5e" + l
                                              : (n = u.toExponential()).slice(0, n.indexOf("e") + 1) + l)
                                  )))
                                : (t = new M(u + "")),
                            t.c[0])
                        )
                            for ((u = (l = t.e) + s) < 3 && (u = 0); ; )
                                if (
                                    ((o = t),
                                    (t = f.times(o.plus(r(a, o, s, 1)))),
                                    d(o.c).slice(0, u) === (n = d(t.c)).slice(0, u))
                                ) {
                                    if ((t.e < l && --u, "9999" != (n = n.slice(u - 3, u + 1)) && (i || "4999" != n))) {
                                        (+n && (+n.slice(1) || "5" != n.charAt(0))) ||
                                            (q(t, t.e + _ + 2, 1), (e = !t.times(t).eq(a)));
                                        break;
                                    }
                                    if (!i && (q(o, o.e + _ + 2, 0), o.times(o).eq(a))) {
                                        t = o;
                                        break;
                                    }
                                    (s += 4), (u += 4), (i = 1);
                                }
                        return q(t, t.e + _ + 1, O, e);
                    }),
                (x.toExponential = function (e, n) {
                    return null != e && (m(e, 0, g), e++), H(this, e, n, 1);
                }),
                (x.toFixed = function (e, n) {
                    return null != e && (m(e, 0, g), (e = e + this.e + 1)), H(this, e, n);
                }),
                (x.toFormat = function (e, n, r) {
                    var t,
                        i = this;
                    if (null == r)
                        null != e && n && "object" == typeof n
                            ? ((r = n), (n = null))
                            : e && "object" == typeof e
                              ? ((r = e), (e = n = null))
                              : (r = R);
                    else if ("object" != typeof r) throw Error(l + "Argument not an object: " + r);
                    if (((t = i.toFixed(e, n)), i.c)) {
                        var o,
                            a = t.split("."),
                            c = +r.groupSize,
                            u = +r.secondaryGroupSize,
                            s = r.groupSeparator || "",
                            f = a[0],
                            h = a[1],
                            g = i.s < 0,
                            p = g ? f.slice(1) : f,
                            d = p.length;
                        if ((u && ((o = c), (c = u), (u = o), (d -= o)), c > 0 && d > 0)) {
                            for (o = d % c || c, f = p.substr(0, o); o < d; o += c) f += s + p.substr(o, c);
                            u > 0 && (f += s + p.slice(o)), g && (f = "-" + f);
                        }
                        t = h
                            ? f +
                              (r.decimalSeparator || "") +
                              ((u = +r.fractionGroupSize)
                                  ? h.replace(
                                        new RegExp("\\d{" + u + "}\\B", "g"),
                                        "$&" + (r.fractionGroupSeparator || "")
                                    )
                                  : h)
                            : f;
                    }
                    return (r.prefix || "") + t + (r.suffix || "");
                }),
                (x.toFraction = function (e) {
                    var n,
                        t,
                        i,
                        o,
                        a,
                        c,
                        u,
                        s,
                        f,
                        g,
                        p,
                        v,
                        m = this,
                        y = m.c;
                    if (null != e && ((!(u = new M(e)).isInteger() && (u.c || 1 !== u.s)) || u.lt(T)))
                        throw Error(l + "Argument " + (u.isInteger() ? "out of range: " : "not an integer: ") + z(u));
                    if (!y) return new M(m);
                    for (
                        n = new M(T),
                            f = t = new M(T),
                            i = s = new M(T),
                            v = d(y),
                            a = n.e = v.length - m.e - 1,
                            n.c[0] = h[(c = a % 14) < 0 ? 14 + c : c],
                            e = !e || u.comparedTo(n) > 0 ? (a > 0 ? n : f) : u,
                            c = D,
                            D = 1 / 0,
                            u = new M(v),
                            s.c[0] = 0;
                        (g = r(u, n, 0, 1)), 1 != (o = t.plus(g.times(i))).comparedTo(e);

                    )
                        (t = i),
                            (i = o),
                            (f = s.plus(g.times((o = f)))),
                            (s = o),
                            (n = u.minus(g.times((o = n)))),
                            (u = o);
                    return (
                        (o = r(e.minus(t), i, 0, 1)),
                        (s = s.plus(o.times(f))),
                        (t = t.plus(o.times(i))),
                        (s.s = f.s = m.s),
                        (p =
                            r(f, i, (a *= 2), O)
                                .minus(m)
                                .abs()
                                .comparedTo(r(s, t, a, O).minus(m).abs()) < 1
                                ? [f, i]
                                : [s, t]),
                        (D = c),
                        p
                    );
                }),
                (x.toNumber = function () {
                    return +z(this);
                }),
                (x.toPrecision = function (e, n) {
                    return null != e && m(e, 1, g), H(this, e, n, 2);
                }),
                (x.toString = function (e) {
                    var n,
                        r = this,
                        i = r.s,
                        o = r.e;
                    return (
                        null === o
                            ? i
                                ? ((n = "Infinity"), i < 0 && (n = "-" + n))
                                : (n = "NaN")
                            : (null == e
                                  ? (n = o <= C || o >= B ? w(d(r.c), o) : S(d(r.c), o, "0"))
                                  : 10 === e
                                    ? (n = S(d((r = q(new M(r), _ + o + 1, O)).c), r.e, "0"))
                                    : (m(e, 2, L.length, "Base"), (n = t(S(d(r.c), o, "0"), 10, e, i, !0))),
                              i < 0 && r.c[0] && (n = "-" + n)),
                        n
                    );
                }),
                (x.valueOf = x.toJSON =
                    function () {
                        return z(this);
                    }),
                (x._isBigNumber = !0),
                (x[Symbol.toStringTag] = "BigNumber"),
                (x[Symbol.for("nodejs.util.inspect.custom")] = x.valueOf),
                null != n && M.set(n),
                M
            );
        })();
    function E(e) {
        if (!e || !/^-?\d*(\.\d+)?$/.test(e)) throw new Error("Invalid value: ".concat(e));
    }
    b.config({ EXPONENTIAL_AT: [-9, 20] }),
        ((A = e.AmountFormat || (e.AmountFormat = {}))[(A.PLANCK = 0)] = "PLANCK"),
        (A[(A.SIGNA = 1)] = "SIGNA");
    var k = (function () {
        function r(e) {
            n(this, r), "string" == typeof e && E(e), (this._planck = new b(e));
        }
        return (
            t(
                r,
                [
                    {
                        key: "getRaw",
                        value: function () {
                            return this._planck;
                        },
                    },
                    {
                        key: "getPlanck",
                        value: function () {
                            return this._planck.toString();
                        },
                    },
                    {
                        key: "setPlanck",
                        value: function (e) {
                            E(e), (this._planck = new b(e));
                        },
                    },
                    {
                        key: "getSigna",
                        value: function () {
                            return this._planck.dividedBy(1e8).toString();
                        },
                    },
                    {
                        key: "setSigna",
                        value: function (e) {
                            E(e), (this._planck = new b(e).multipliedBy(1e8));
                        },
                    },
                    {
                        key: "equals",
                        value: function (e) {
                            return this._planck.eq(e._planck);
                        },
                    },
                    {
                        key: "lessOrEqual",
                        value: function (e) {
                            return this._planck.lte(e._planck);
                        },
                    },
                    {
                        key: "less",
                        value: function (e) {
                            return this._planck.lt(e._planck);
                        },
                    },
                    {
                        key: "greaterOrEqual",
                        value: function (e) {
                            return this._planck.gte(e._planck);
                        },
                    },
                    {
                        key: "greater",
                        value: function (e) {
                            return this._planck.gt(e._planck);
                        },
                    },
                    {
                        key: "add",
                        value: function (e) {
                            return (this._planck = this._planck.plus(e._planck)), this;
                        },
                    },
                    {
                        key: "subtract",
                        value: function (e) {
                            return (this._planck = this._planck.minus(e._planck)), this;
                        },
                    },
                    {
                        key: "multiply",
                        value: function (e) {
                            return (this._planck = this._planck.multipliedBy(e)), this;
                        },
                    },
                    {
                        key: "divide",
                        value: function (e) {
                            if (0 === e) throw new Error("Division by zero");
                            return (this._planck = this._planck.div(e)), this;
                        },
                    },
                    {
                        key: "toString",
                        value: function () {
                            var n =
                                arguments.length > 0 && void 0 !== arguments[0] ? arguments[0] : e.AmountFormat.SIGNA;
                            return n === e.AmountFormat.SIGNA
                                ? "".concat("Ꞩ", " ").concat(this.getSigna())
                                : "".concat("ꞩ", " ").concat(this._planck);
                        },
                    },
                    {
                        key: "clone",
                        value: function () {
                            return r.fromPlanck(this.getPlanck());
                        },
                    },
                ],
                [
                    {
                        key: "CurrencySymbol",
                        value: function () {
                            return "Ꞩ";
                        },
                    },
                    {
                        key: "Zero",
                        value: function () {
                            return new r("0");
                        },
                    },
                    {
                        key: "fromPlanck",
                        value: function (e) {
                            return new r(e);
                        },
                    },
                    {
                        key: "fromSigna",
                        value: function (e) {
                            var n = new r("0");
                            return n.setSigna("number" == typeof e ? e.toString(10) : e), n;
                        },
                    },
                ]
            ),
            r
        );
    })();
    const N = "function" == typeof atob,
        x = "function" == typeof btoa,
        T = "function" == typeof Buffer,
        _ = "function" == typeof TextDecoder ? new TextDecoder() : void 0,
        O = "function" == typeof TextEncoder ? new TextEncoder() : void 0,
        C = [..."ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="],
        B = ((e) => {
            let n = {};
            return e.forEach((e, r) => (n[e] = r)), n;
        })(C),
        F = /^(?:[A-Za-z\d+\/]{4})*?(?:[A-Za-z\d+\/]{2}(?:==)?|[A-Za-z\d+\/]{3}=?)?$/,
        D = String.fromCharCode.bind(String),
        P =
            "function" == typeof Uint8Array.from
                ? Uint8Array.from.bind(Uint8Array)
                : (e, n = (e) => e) => new Uint8Array(Array.prototype.slice.call(e, 0).map(n)),
        I = (e) => e.replace(/[+\/]/g, (e) => ("+" == e ? "-" : "_")).replace(/=+$/m, ""),
        U = (e) => e.replace(/[^A-Za-z0-9\+\/]/g, ""),
        R = (e) => {
            let n,
                r,
                t,
                i,
                o = "";
            const a = e.length % 3;
            for (let a = 0; a < e.length; ) {
                if ((r = e.charCodeAt(a++)) > 255 || (t = e.charCodeAt(a++)) > 255 || (i = e.charCodeAt(a++)) > 255)
                    throw new TypeError("invalid character found");
                (n = (r << 16) | (t << 8) | i),
                    (o += C[(n >> 18) & 63] + C[(n >> 12) & 63] + C[(n >> 6) & 63] + C[63 & n]);
            }
            return a ? o.slice(0, a - 3) + "===".substring(a) : o;
        },
        L = x ? (e) => btoa(e) : T ? (e) => Buffer.from(e, "binary").toString("base64") : R,
        M = T
            ? (e) => Buffer.from(e).toString("base64")
            : (e) => {
                  let n = [];
                  for (let r = 0, t = e.length; r < t; r += 4096) n.push(D.apply(null, e.subarray(r, r + 4096)));
                  return L(n.join(""));
              },
        H = (e) => {
            if (e.length < 2)
                return (n = e.charCodeAt(0)) < 128
                    ? e
                    : n < 2048
                      ? D(192 | (n >>> 6)) + D(128 | (63 & n))
                      : D(224 | ((n >>> 12) & 15)) + D(128 | ((n >>> 6) & 63)) + D(128 | (63 & n));
            var n = 65536 + 1024 * (e.charCodeAt(0) - 55296) + (e.charCodeAt(1) - 56320);
            return (
                D(240 | ((n >>> 18) & 7)) + D(128 | ((n >>> 12) & 63)) + D(128 | ((n >>> 6) & 63)) + D(128 | (63 & n))
            );
        },
        G = /[\uD800-\uDBFF][\uDC00-\uDFFFF]|[^\x00-\x7F]/g,
        j = (e) => e.replace(G, H),
        q = T ? (e) => Buffer.from(e, "utf8").toString("base64") : O ? (e) => M(O.encode(e)) : (e) => L(j(e)),
        z = (e, n = !1) => (n ? I(q(e)) : q(e)),
        $ = (e) => z(e, !0),
        V = /[\xC0-\xDF][\x80-\xBF]|[\xE0-\xEF][\x80-\xBF]{2}|[\xF0-\xF7][\x80-\xBF]{3}/g,
        Z = (e) => {
            switch (e.length) {
                case 4:
                    var n =
                        (((7 & e.charCodeAt(0)) << 18) |
                            ((63 & e.charCodeAt(1)) << 12) |
                            ((63 & e.charCodeAt(2)) << 6) |
                            (63 & e.charCodeAt(3))) -
                        65536;
                    return D(55296 + (n >>> 10)) + D(56320 + (1023 & n));
                case 3:
                    return D(((15 & e.charCodeAt(0)) << 12) | ((63 & e.charCodeAt(1)) << 6) | (63 & e.charCodeAt(2)));
                default:
                    return D(((31 & e.charCodeAt(0)) << 6) | (63 & e.charCodeAt(1)));
            }
        },
        W = (e) => e.replace(V, Z),
        Q = (e) => {
            if (((e = e.replace(/\s+/g, "")), !F.test(e))) throw new TypeError("malformed base64.");
            e += "==".slice(2 - (3 & e.length));
            let n,
                r,
                t,
                i = "";
            for (let o = 0; o < e.length; )
                (n =
                    (B[e.charAt(o++)] << 18) |
                    (B[e.charAt(o++)] << 12) |
                    ((r = B[e.charAt(o++)]) << 6) |
                    (t = B[e.charAt(o++)])),
                    (i +=
                        64 === r
                            ? D((n >> 16) & 255)
                            : 64 === t
                              ? D((n >> 16) & 255, (n >> 8) & 255)
                              : D((n >> 16) & 255, (n >> 8) & 255, 255 & n));
            return i;
        },
        J = N ? (e) => atob(U(e)) : T ? (e) => Buffer.from(e, "base64").toString("binary") : Q,
        X = T ? (e) => P(Buffer.from(e, "base64")) : (e) => P(J(e), (e) => e.charCodeAt(0)),
        K = T ? (e) => Buffer.from(e, "base64").toString("utf8") : _ ? (e) => _.decode(X(e)) : (e) => W(J(e)),
        Y = (e) => U(e.replace(/[-_]/g, (e) => ("-" == e ? "+" : "/"))),
        ee = (e) => K(Y(e)),
        ne = z,
        re = $,
        te = ee;
    var ie = function (e) {
            return te(e);
        },
        oe = function (e) {
            for (
                var n = arguments.length > 1 && void 0 !== arguments[1] && arguments[1], r = [], t = 0;
                t < e.length;
                t++
            )
                r.push((e[t] >>> 4).toString(16)), r.push((15 & e[t]).toString(16));
            return n ? r.join("").toUpperCase() : r.join("");
        },
        ae = function (e) {
            var n = arguments.length > 1 && void 0 !== arguments[1] ? arguments[1] : 0,
                r = arguments.length > 2 && void 0 !== arguments[2] ? arguments[2] : null;
            if (0 === r) return "";
            var t = e;
            if (0 !== n) {
                var i = null === r ? e.length - n : r;
                ce(t, i, n), (t = e.slice(n, n + i));
            }
            return decodeURIComponent(escape(String.fromCharCode.apply(null, Array.from(t))));
        };
    function ce(e, n) {
        var r = arguments.length > 2 && void 0 !== arguments[2] ? arguments[2] : 0;
        if (r < 0) throw new Error("Start index should not be negative");
        if (e.length < r + n) throw new Error("Need at least " + n + " bytes to convert to an integer");
        return r;
    }
    String.prototype.padStart ||
        (String.prototype.padStart = function (e, n) {
            return (
                (e >>= 0),
                (n = String(void 0 !== n ? n : " ")),
                this.length >= e
                    ? String(this)
                    : ((e -= this.length) > n.length && (n += n.repeat(e / n.length)), n.slice(0, e) + String(this))
            );
        });
    var ue,
        le = function (e) {
            for (var n = e.multipliedBy(-1).toString(2); n.length % 8; ) n = "0" + n;
            var r = "1" === n[0] && -1 !== n.slice(1).indexOf("1") ? "11111111" : "";
            return (
                (n = n
                    .split("")
                    .map(function (e) {
                        return "0" === e ? "1" : "0";
                    })
                    .join("")),
                new b(r + n, 2).plus(1)
            );
        },
        se = function (e) {
            if (e.length % 2) throw new Error("Invalid Hex String: ".concat(e));
            for (var n = new Uint8Array(e.length / 2), r = 0; r < e.length; r += 2) {
                var t = parseInt(e.substr(r, 2), 16);
                if (Number.isNaN(t)) throw new Error("Invalid Hex String: ".concat(e));
                n[r / 2] = t;
            }
            return n;
        },
        fe = function (e) {
            return ae(se(e));
        },
        he = function (e) {
            if (null == e || "" === e) throw new Error("Invalid argument");
            return parseFloat(e) / 1e8;
        },
        ge = function (e) {
            var n = !(arguments.length > 1 && void 0 !== arguments[1]) || arguments[1];
            return n ? re(e) : ne(e);
        },
        pe = function (e) {
            for (var n = unescape(encodeURIComponent(e)), r = new Uint8Array(n.length), t = 0; t < n.length; ++t)
                r[t] = n.charCodeAt(t);
            return r;
        },
        de = function (e) {
            return oe(pe(e));
        };
    ((ue = e.EncoderFormat || (e.EncoderFormat = {}))[(ue.Text = 0)] = "Text"),
        (ue[(ue.Hexadecimal = 1)] = "Hexadecimal"),
        (ue[(ue.Base64 = 2)] = "Base64");
    var ve = /^util$.?(.+)?:\/\/(v.+?)\??/i;
    (e.Amount = k),
        (e.ChainTime = o),
        (e.CurrencyPlanckSymbol = "ꞩ"),
        (e.CurrencySymbol = "Ꞩ"),
        (e.FeeQuantPlanck = 735e3),
        (e.OneSignaPlanck = 1e8),
        (e.asyncRetry = function e(n) {
            return (
                (r = this),
                (t = void 0),
                (o = function* () {
                    var r = n.asyncFn,
                        t = n.onFailureAsync,
                        i = n.retryCount,
                        o = void 0 === i ? 1 : i,
                        a = n.maxRetrials,
                        c = void 0 === a ? 20 : a;
                    try {
                        return yield r();
                    } catch (n) {
                        if (o > c) throw n;
                        if (!(yield t(n, o))) throw n;
                        yield e({ asyncFn: r, onFailureAsync: t, retryCount: o + 1 });
                    }
                }),
                new ((i = void 0) || (i = Promise))(function (e, n) {
                    function a(e) {
                        try {
                            u(o.next(e));
                        } catch (e) {
                            n(e);
                        }
                    }
                    function c(e) {
                        try {
                            u(o.throw(e));
                        } catch (e) {
                            n(e);
                        }
                    }
                    function u(n) {
                        n.done
                            ? e(n.value)
                            : new i(function (e) {
                                  e(n.value);
                              }).then(a, c);
                    }
                    u((o = o.apply(r, t || [])).next());
                })
            );
            var r, t, i, o;
        }),
        (e.convertBase64StringToString = ie),
        (e.convertByteArrayToHexString = oe),
        (e.convertByteArrayToString = ae),
        (e.convertDecStringToHexString = function (e) {
            var n = arguments.length > 1 && void 0 !== arguments[1] ? arguments[1] : 2,
                r = "string" == typeof e ? new b(e) : e;
            if (r.isNaN())
                throw new Error("Invalid decimal argument: [".concat(e, "] - Expected a valid decimal value"));
            if (n < 0) throw new Error("Invalid padding argument: [".concat(n, "] - Expected a positive value"));
            var t = r.lt(0);
            t && (r = le(r));
            var i = r.toString(16),
                o = Math.ceil(i.length / n);
            return i.padStart(o * n, t ? "f" : "0");
        }),
        (e.convertHexEndianess = function (e) {
            for (var n = "", r = e, t = r.length - 1; t >= 0; t -= 2) n += r[t - 1] + r[t];
            return n;
        }),
        (e.convertHexStringToByteArray = se),
        (e.convertHexStringToDecString = function (e) {
            var n,
                r,
                t,
                i = [0];
            for (n = 0; n < e.length; n += 1) {
                for (t = parseInt(e.charAt(n), 16), r = 0; r < i.length; r += 1)
                    (i[r] = 16 * i[r] + t), (t = (i[r] / 10) | 0), (i[r] %= 10);
                for (; t > 0; ) i.push(t % 10), (t = (t / 10) | 0);
            }
            return i.reverse().join("");
        }),
        (e.convertHexStringToString = fe),
        (e.convertNQTStringToNumber = he),
        (e.convertNumberToNQTString = function (e) {
            if (null == e) throw new Error("Invalid argument");
            return parseFloat(e.toString()).toFixed(8).replace(".", "");
        }),
        (e.convertStringToBase64String = ge),
        (e.convertStringToByteArray = pe),
        (e.convertStringToHexString = de)
        (e.sumAnniesStringToNumber = function () {
            for (var e = arguments.length, n = new Array(e), r = 0; r < e; r++) n[r] = arguments[r];
            return n.reduce(function (e, n) {
                return e + he(n);
            }, 0);
        }),
        Object.defineProperty(e, "__esModule", { value: !0 });
});