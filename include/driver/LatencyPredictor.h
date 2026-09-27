// SPDX-License-Identifier: AGPL-3.0-only
// Added by Shinyflvres, 2026-09-27. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md

#pragma once

#include "PoseMath.h"

#include <algorithm>
#include <cmath>

namespace latency
{
	struct Rls3
	{
		double th[3] = { 0.0, 0.0, 0.0 };
		double P[3][3] = {};

		void init(double tv)
		{
			th[0] = tv; th[1] = 0.0; th[2] = 0.0;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					P[i][j] = 0.0;
			P[0][0] = 1e-3; P[1][1] = 1e-5; P[2][2] = 1e-5;
		}

		void forget(double lambda, double pMax)
		{
			for (auto& r : P)
				for (auto& c : r)
					c /= lambda;
			double tr = P[0][0] + P[1][1] + P[2][2];
			if (tr > pMax)
			{
				double s = pMax / tr;
				for (auto& r : P)
					for (auto& c : r)
						c *= s;
			}
		}

		void update(const double* f, double y)
		{
			double Pf[3], denom = 1.0, pred = 0.0;
			for (int i = 0; i < 3; i++)
			{
				Pf[i] = P[i][0] * f[0] + P[i][1] * f[1] + P[i][2] * f[2];
				denom += f[i] * Pf[i];
				pred += th[i] * f[i];
			}
			double err = y - pred;
			for (int i = 0; i < 3; i++)
				th[i] += Pf[i] / denom * err;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					P[i][j] -= Pf[i] * Pf[j] / denom;
		}
	};

	struct NoiseFloor
	{
		double initial = 0.1;
		double est = 0.1;
		double sum = 0.0;
		int n = 0;
		double start = -1.0;

		void reset() { est = initial; sum = 0.0; n = 0; start = -1.0; }

		void add(double magnitude, double t)
		{
			if (start < 0.0) start = t;
			sum += magnitude * magnitude;
			n++;
			if (t - start >= 0.1)
			{
				double rms = std::sqrt(sum / n);
				est = rms < est ? rms : est * 1.0005;
				sum = 0.0;
				n = 0;
				start = t;
			}
		}
	};

	struct Channel
	{
		static const int Ring = 512;

		struct Entry
		{
			double t = 0.0;
			vr::HmdVector3d_t x = { 0, 0, 0 };
			vr::HmdQuaternion_t q = { 1, 0, 0, 0 };
			vr::HmdVector3d_t v = { 0, 0, 0 };
			vr::HmdVector3d_t a = { 0, 0, 0 };
			vr::HmdVector3d_t a2 = { 0, 0, 0 };
			vr::HmdVector3d_t d = { 0, 0, 0 };
			double w = 0.0;
		};

		bool rotation = false;
		double gateK = 5.0;
		double brakeK = 0.8;
		double forgetSeconds = 5.0;
		double accelSlow = 0.015;
		double accelFast = 0.005;
		double directionSeconds = 0.030;
		double outputHz = 10.0;

		Entry ring[Ring];
		long long seqNext = 0;
		long long cursor = -1;
		bool primed = false;
		bool learnerReady = false;
		double lastT = 0.0;
		vr::HmdVector3d_t aSlow = { 0, 0, 0 }, aFast = { 0, 0, 0 }, vPrev = { 0, 0, 0 }, vDir = { 0, 0, 0 }, dOut = { 0, 0, 0 };
		NoiseFloor floorV{ 0.1, 0.1 }, floorA{ 10.0, 10.0 }, floorA2{ 10.0, 10.0 };
		Rls3 rls;

		double errWithout = 0.0, errWith = 0.0;
		long errCount = 0;
		double lastGate = 0.0;

		void reset()
		{
			seqNext = 0;
			cursor = -1;
			primed = false;
			learnerReady = false;
			aSlow = aFast = vPrev = vDir = dOut = { 0, 0, 0 };
			floorV.reset();
			floorA.reset();
			floorA2.reset();
		}

		const Entry& entry(long long seq) const { return ring[seq % Ring]; }

		const Entry* lookup(double target)
		{
			long long oldest = seqNext - Ring > 0 ? seqNext - Ring : 0;
			if (seqNext == 0)
				return nullptr;
			if (cursor < oldest) cursor = oldest;
			if (cursor >= seqNext) cursor = seqNext - 1;
			while (cursor > oldest && entry(cursor).t > target)
				cursor--;
			while (cursor + 1 < seqNext && entry(cursor + 1).t <= target)
				cursor++;
			const Entry& e = entry(cursor);
			if (e.t > target || target - e.t > 0.01)
				return nullptr;
			return &e;
		}

		vr::HmdVector3d_t displacement(const Entry& from, const vr::HmdVector3d_t& x, const vr::HmdQuaternion_t& q) const
		{
			if (rotation)
				return quaternionToRotationVector(quaternionNormalize(q * quaternionConjugate(from.q)));
			return vecSub(x, from.x);
		}

		static double Alpha(double dt, double timeConstant) { return 1.0 - std::exp(-dt / timeConstant); }

		vr::HmdVector3d_t step(double t, const vr::HmdVector3d_t& x, const vr::HmdQuaternion_t& q, const vr::HmdVector3d_t& v, double tau, double hs)
		{
			double dt = primed ? t - lastT : 0.002;
			if (primed && dt > 0.1)
			{
				reset();
				dt = 0.002;
			}
			if (dt <= 0.0)
			{
				dt = 0.0005;
				t = lastT + dt;
			}

			if (!primed)
			{
				vPrev = v;
				vDir = v;
				primed = true;
			}
			else
			{
				vr::HmdVector3d_t dv = vecScale(vecSub(v, vPrev), 1.0 / dt);
				aSlow = vecAdd(aSlow, vecScale(vecSub(dv, aSlow), Alpha(dt, accelSlow)));
				aFast = vecAdd(aFast, vecScale(vecSub(dv, aFast), Alpha(dt, accelFast)));
			}
			vPrev = v;
			vDir = vecAdd(vDir, vecScale(vecSub(v, vDir), Alpha(dt, directionSeconds)));
			vr::HmdVector3d_t a = aSlow;
			vr::HmdVector3d_t a2 = vecSub(aFast, aSlow);

			double sv = vecNorm(v), sa = vecNorm(a), sa2 = vecNorm(a2);
			floorV.add(sv, t);
			floorA.add(sa, t);
			floorA2.add(sa2, t);
			double z2 = sv * sv / (floorV.est * floorV.est) + sa * sa / (floorA.est * floorA.est) + sa2 * sa2 / (floorA2.est * floorA2.est);
			double w = (std::max)(0.0, 1.0 - gateK * gateK / (std::max)(1e-12, z2));
			lastGate = w;

			double H = hs + tau;
			if (!learnerReady)
			{
				rls.init(tau);
				learnerReady = true;
			}

			const Entry* o = lookup(t - H);
			if (o)
			{
				rls.forget(std::exp(-dt / forgetSeconds), 1.0);
				vr::HmdVector3d_t disp = displacement(*o, x, q);
				for (int ax = 0; ax < 3; ax++)
				{
					double y = disp.v[ax] - o->v.v[ax] * hs;
					double f[3] = { o->v.v[ax] * o->w, o->a.v[ax] * o->w, o->a2.v[ax] * o->w };
					rls.update(f, y);
				}
				vr::HmdVector3d_t without = vecSub(disp, vecScale(o->v, hs));
				vr::HmdVector3d_t with = vecSub(without, o->d);
				errWithout += vecDot(without, without);
				errWith += vecDot(with, with);
				errCount++;
			}

			double tv = (std::min)(1.5 * tau, (std::max)(0.0, rls.th[0]));
			double ta = (std::min)(H * H, (std::max)(-H * H, rls.th[1]));
			double ta2 = (std::min)(H * H, (std::max)(-H * H, rls.th[2]));
			vr::HmdVector3d_t d = vecAdd(vecAdd(vecScale(v, tv * w), vecScale(a, ta * w)), vecScale(a2, ta2 * w));

			double dirNorm = vecNorm(vDir);
			if (brakeK > 0.0 && dirNorm > 1e-9)
			{
				vr::HmdVector3d_t dir = vecScale(vDir, 1.0 / dirNorm);
				double along = vecDot(aFast, dir);
				double total = vecDot(vecAdd(vecScale(v, hs), d), dir);
				double speedAlong = (std::max)(0.0, vecDot(v, dir));
				if (along < 0.0 && total > 0.0)
				{
					double stop = brakeK * speedAlong * speedAlong / (2.0 * -along);
					double own = vecDot(d, dir);
					if (total > stop && own > 0.0)
						d = vecSub(d, vecScale(dir, (std::min)(total - stop, own)));
				}
			}

			dOut = vecAdd(dOut, vecScale(vecSub(d, dOut), 1.0 - std::exp(-dt * 2.0 * POSE_PI * outputHz)));

			Entry& e = ring[seqNext % Ring];
			e.t = t;
			e.x = x;
			e.q = q;
			e.v = v;
			e.a = a;
			e.a2 = a2;
			e.d = dOut;
			e.w = w;
			seqNext++;
			lastT = t;
			return dOut;
		}
	};

	struct Device
	{
		Channel position;
		Channel rotation;
		double lastLog = 0.0;

		Device() { rotation.rotation = true; }

		void reset()
		{
			position.reset();
			rotation.reset();
		}
	};
}
