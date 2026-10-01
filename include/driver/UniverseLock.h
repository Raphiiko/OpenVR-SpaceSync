// SPDX-License-Identifier: AGPL-3.0-only
// Added by Shinyflvres, 2026-09-30. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md

#pragma once

#include "PoseMath.h"

#include <openvr_driver.h>

#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace universe
{
	struct Frame
	{
		vr::HmdQuaternion_t q = { 1, 0, 0, 0 };
		vr::HmdVector3d_t t = { 0, 0, 0 };
	};

	inline Frame FrameMul(const Frame& a, const Frame& b)
	{
		return { quaternionNormalize(a.q * b.q), vecAdd(a.t, quaternionRotateVector(a.q, b.t)) };
	}

	inline Frame FrameInv(const Frame& a)
	{
		vr::HmdQuaternion_t c = quaternionConjugate(a.q);
		return { c, vecScale(quaternionRotateVector(c, a.t), -1.0) };
	}

	inline vr::HmdVector3d_t FrameApply(const Frame& f, const vr::HmdVector3d_t& p)
	{
		return vecAdd(f.t, quaternionRotateVector(f.q, p));
	}

	inline double FrameAngle(const Frame& a, const Frame& b)
	{
		return quaternionAngleRad(quaternionNormalize(a.q * quaternionConjugate(b.q)));
	}

	inline double FrameDistance(const Frame& a, const Frame& b)
	{
		return vecNorm(vecSub(a.t, b.t)) + FrameAngle(a, b);
	}

	inline Frame FrameBlend(const Frame& a, const Frame& b, double w)
	{
		vr::HmdQuaternion_t qb = b.q;
		if (a.q.w * qb.w + a.q.x * qb.x + a.q.y * qb.y + a.q.z * qb.z < 0.0)
			qb = { -qb.w, -qb.x, -qb.y, -qb.z };
		vr::HmdVector3d_t rv = quaternionToRotationVector(quaternionNormalize(qb * quaternionConjugate(a.q)));
		Frame r;
		r.q = quaternionNormalize(quaternionFromRotationVector(vecScale(rv, w)) * a.q);
		r.t = vecAdd(vecScale(a.t, 1.0 - w), vecScale(b.t, w));
		return r;
	}

	struct LockStatus
	{
		int state = 0;
		uint32_t bases = 0;
		uint32_t heldJumps = 0;
		double largestHeldM = 0.0;
		double offsetM = 0.0;
		double offsetDeg = 0.0;
		double inconsistentM = 0.0;
	};

	class Lock
	{
	public:
		std::function<void(uint32_t, int32_t&, std::string&, std::string&)> properties;
		std::function<void(const std::string&)> log;
		std::string path = "spacesync_lock.txt";

		void configure(bool on, bool calibrating, uint32_t command)
		{
			std::lock_guard<std::mutex> lock(mutex);
			ensureLoaded();
			if (on != enabled)
				say(on ? "Lock Base Stations: enabled" : "Lock Base Stations: disabled");
			enabled = on;
			if (calibrating != paused)
				say(calibrating ? "Lock Base Stations: paused while calibrating" : "Lock Base Stations: resumed");
			paused = calibrating;
			if (command == 1)
				commit();
		}

		LockStatus status()
		{
			std::lock_guard<std::mutex> lock(mutex);
			ensureLoaded();
			if (dirty)
				save();
			LockStatus s;
			s.state = !enabled ? 0 : (refs.empty() ? 1 : (paused ? 3 : 2));
			s.bases = (uint32_t)refs.size();
			s.heldJumps = heldJumps;
			s.largestHeldM = largestHeldM;
			s.offsetM = offsetM;
			s.offsetDeg = offsetDeg * 180.0 / POSE_PI;
			s.inconsistentM = largestDisagreeM;
			offsetM = 0.0;
			offsetDeg = 0.0;
			return s;
		}

		void apply(uint32_t id, vr::DriverPose_t& pose, double now, uint32_t hmdID)
		{
			if (id >= vr::k_unMaxTrackedDeviceCount)
				return;
			std::lock_guard<std::mutex> lock(mutex);
			Dev& d = devs[id];
			if (d.kind == Unknown || (d.kind != Ignored && d.serial.empty() && now - d.classifiedAt > 2.0))
				classify(id, d, now, hmdID);
			if (d.kind == Base)
			{
				recordBase(d, pose);
				return;
			}
			if (d.kind != Device)
				return;

			Frame raw = { quaternionNormalize(pose.qWorldFromDriverRotation), vecFromArray(pose.vecWorldFromDriverTranslation) };
			vr::HmdVector3d_t local = vecAdd(vecFromArray(pose.vecPosition), quaternionRotateVector(pose.qRotation, pose.vecDriverFromHeadTranslation));
			vr::HmdQuaternion_t localRot = quaternionNormalize(pose.qRotation * pose.qDriverFromHeadRotation);
			bool valid = pose.poseIsValid;

			Frame out = raw;
			bool active = enabled && !paused && !refs.empty() && loaded;
			if (valid)
			{
				bool fresh = d.primed && d.lastValid && now - d.lastT < 0.1;
				bool frameChanged = !d.primed || FrameDistance(raw, d.lastRaw) > 1e-7;
				bool continuous = false;
				if (fresh)
				{
					double dt = now - d.lastT;
					vr::HmdVector3d_t predicted = vecAdd(d.lastLocal, vecScale(vecFromArray(pose.vecVelocity), dt));
					double step = vecNorm(vecSub(local, predicted));
					double turn = quaternionAngleRad(quaternionNormalize(localRot * quaternionConjugate(d.lastLocalRot)));
					continuous = step < 0.03 && turn < 3.0 * POSE_PI / 180.0;
				}

				bool sameBaseMove = fresh && frameChanged && continuous;
				if (frameChanged || d.base.empty())
				{
					std::string before = d.base;
					if (!(sameBaseMove && !d.base.empty()))
						d.base = identify(raw);
					if (d.primed && d.base != before)
					{
						d.fadeFromBase = before;
						startFade(d, now);
					}

					if (sameBaseMove && active)
					{
						double movedM = vecNorm(vecSub(FrameApply(raw, local), FrameApply(d.lastRaw, local)));
						if (movedM > 0.005)
						{
							heldJumps++;
							if (movedM > largestHeldM)
								largestHeldM = movedM;
							if (now - lastHeldLog > 1.0 || movedM > 0.2)
							{
								lastHeldLog = now;
								char buf[256];
								std::snprintf(buf, sizeof buf, "Lock Base Stations: held a SteamVR move of base %s: %.1f cm / %.2f deg at device %u",
									d.base.empty() ? "(unidentified)" : d.base.c_str(), movedM * 100.0, FrameAngle(raw, d.lastRaw) * 180.0 / POSE_PI, id);
								say(buf);
							}
						}
					}
				}

				if (active)
				{
					Frame keep = d.primed && d.lastActive ? FrameMul(d.lastOut, FrameInv(d.lastRaw)) : universeFix;
					if (!d.base.empty())
					{
						auto it = refs.find(d.base);
						if (it == refs.end())
						{
							Frame k = sameBaseMove && d.lastActive ? FrameMul(d.lastOut, FrameInv(raw)) : keep;
							refs[d.base] = FrameMul(k, raw);
							dirty = true;
							char buf[160];
							std::snprintf(buf, sizeof buf, "Lock Base Stations: base %s locked on first use", d.base.c_str());
							say(buf);
							it = refs.find(d.base);
						}
						out = it->second;
						universeFix = FrameMul(out, FrameInv(raw));
					}
					else if (sameBaseMove && d.lastActive)
						out = d.lastOut;
					else
						out = FrameMul(keep, raw);

					if (d.fadeStart >= 0.0 && d.fadeLogged == false)
					{
						d.fadeLogged = true;
						double dtFade = now - d.lastT;
						if (dtFade < 0.0 || dtFade > 0.1)
							dtFade = 0.0;
						vr::HmdVector3d_t predictedLocal = vecAdd(d.lastLocal, vecScale(vecFromArray(pose.vecVelocity), dtFade));
						Frame previousPose = FrameMul(d.lastOut, Frame{ d.lastLocalRot, predictedLocal });
						Frame newPose = FrameMul(out, Frame{ localRot, local });
						d.fadeFrom = FrameMul(previousPose, FrameInv(newPose));
						double glide = vecNorm(vecSub(previousPose.t, newPose.t));
						d.fadeDuration = (std::min)(1.0, fadeSeconds + glide);
						if (glide > largestDisagreeM)
							largestDisagreeM = glide;
						if (glide > 0.01)
						{
							char buf[320];
							std::snprintf(buf, sizeof buf, "Lock Base Stations: device %u switched reference base %s -> %s, the two locked positions disagree by %.1f cm here, blending over %.0f ms%s",
								id, d.fadeFromBase.empty() ? "(unidentified)" : d.fadeFromBase.c_str(), d.base.empty() ? "(unidentified)" : d.base.c_str(), glide * 100.0, d.fadeDuration * 1000.0,
								glide > 0.1 ? " - the SteamVR base station layout saved at calibration is inconsistent, redo SteamVR Room Setup and calibrate again" : "");
							say(buf);
						}
					}
					if (d.fadeStart >= 0.0)
					{
						double w = (now - d.fadeStart) / d.fadeDuration;
						if (w >= 1.0 || w < 0.0)
							d.fadeStart = -1.0;
						else
							out = FrameMul(FrameBlend(d.fadeFrom, Frame{}, w * w * (3.0 - 2.0 * w)), out);
					}

					vr::HmdVector3d_t world = FrameApply(raw, local);
					double off = vecNorm(vecSub(FrameApply(out, local), world));
					double ang = FrameAngle(out, raw);
					if (off > offsetM) offsetM = off;
					if (ang > offsetDeg) offsetDeg = ang;
				}

				d.primed = true;
				d.lastRaw = raw;
				d.lastLocal = local;
				d.lastLocalRot = localRot;
				d.lastT = now;
				d.lastOut = out;
				d.lastActive = active;
			}
			else if (active && d.primed && d.lastActive)
			{
				out = FrameMul(FrameMul(d.lastOut, FrameInv(d.lastRaw)), raw);
			}
			d.lastValid = valid;

			pose.qWorldFromDriverRotation = out.q;
			pose.vecWorldFromDriverTranslation[0] = out.t.v[0];
			pose.vecWorldFromDriverTranslation[1] = out.t.v[1];
			pose.vecWorldFromDriverTranslation[2] = out.t.v[2];
		}

	private:
		enum Kind { Unknown, Ignored, Base, Device };

		struct Dev
		{
			Kind kind = Unknown;
			std::string serial;
			double classifiedAt = -1e9;
			std::deque<Frame> reports;
			bool primed = false;
			bool lastValid = false;
			bool lastActive = false;
			Frame lastRaw, lastOut;
			vr::HmdVector3d_t lastLocal = { 0, 0, 0 };
			vr::HmdQuaternion_t lastLocalRot = { 1, 0, 0, 0 };
			double lastT = -1e9;
			std::string base;
			double fadeStart = -1.0;
			Frame fadeFrom;
			bool fadeLogged = true;
			double fadeDuration = 0.15;
			std::string fadeFromBase;
		};

		std::mutex mutex;
		Dev devs[vr::k_unMaxTrackedDeviceCount];
		std::map<std::string, Frame> refs;
		Frame universeFix;
		bool enabled = false;
		bool paused = false;
		bool loaded = false;
		bool dirty = false;
		uint32_t heldJumps = 0;
		double largestHeldM = 0.0;
		double offsetM = 0.0, offsetDeg = 0.0;
		double lastHeldLog = -1e9;
		double largestDisagreeM = 0.0;
		const double fadeSeconds = 0.15;
		const double identifyTolerance = 0.3;

		void say(const std::string& s)
		{
			if (log)
				log(s);
		}

		void classify(uint32_t id, Dev& d, double now, uint32_t hmdID)
		{
			d.classifiedAt = now;
			int32_t cls = 0;
			std::string system, serial;
			if (properties)
				properties(id, cls, system, serial);
			else
			{
				vr::PropertyContainerHandle_t c = vr::VRProperties()->TrackedDeviceToPropertyContainer(id);
				vr::ETrackedPropertyError err = vr::TrackedProp_Success;
				cls = vr::VRProperties()->GetInt32Property(c, vr::Prop_DeviceClass_Int32, &err);
				system = vr::VRProperties()->GetStringProperty(c, vr::Prop_TrackingSystemName_String, &err);
				serial = vr::VRProperties()->GetStringProperty(c, vr::Prop_SerialNumber_String, &err);
			}
			d.serial = serial;
			if (id == hmdID || system != "lighthouse")
				d.kind = Ignored;
			else
				d.kind = cls == vr::TrackedDeviceClass_TrackingReference ? Base : Device;
		}

		void recordBase(Dev& d, const vr::DriverPose_t& pose)
		{
			vr::HmdQuaternion_t q = pose.qWorldFromDriverRotation;
			double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			if (!pose.poseIsValid || n < 0.5 || d.serial.empty())
				return;
			Frame f = { quaternionNormalize(pose.qWorldFromDriverRotation * pose.qRotation), FrameApply({ quaternionNormalize(pose.qWorldFromDriverRotation), vecFromArray(pose.vecWorldFromDriverTranslation) }, vecFromArray(pose.vecPosition)) };
			if (!d.reports.empty() && FrameDistance(d.reports.back(), f) < 1e-6)
				return;
			d.reports.push_back(f);
			if (d.reports.size() > 16)
				d.reports.pop_front();
		}

		std::string identify(const Frame& raw)
		{
			std::string best;
			double bestD = identifyTolerance;
			for (const Dev& b : devs)
			{
				if (b.kind != Base)
					continue;
				for (const Frame& r : b.reports)
				{
					double dd = FrameDistance(r, raw);
					if (dd < bestD)
					{
						bestD = dd;
						best = b.serial;
					}
				}
			}
			return best;
		}

		void startFade(Dev& d, double now)
		{
			if (!d.lastActive || !d.lastValid || now - d.lastT > 0.5)
				return;
			d.fadeStart = now;
			d.fadeLogged = false;
		}

		void commit()
		{
			refs.clear();
			for (Dev& d : devs)
			{
				if (d.kind != Device || !d.primed || d.base.empty())
					continue;
				refs[d.base] = d.lastRaw;
			}
			for (Dev& d : devs)
				if (d.kind == Device && d.primed)
				{
					d.lastOut = d.lastRaw;
					d.fadeStart = -1.0;
				}
			universeFix = Frame{};
			heldJumps = 0;
			largestHeldM = 0.0;
			largestDisagreeM = 0.0;
			save();
			char buf[160];
			std::snprintf(buf, sizeof buf, "Lock Base Stations: reference taken from the new calibration (%u base stations)", (uint32_t)refs.size());
			say(buf);
		}

		void ensureLoaded()
		{
			if (loaded)
				return;
			loaded = true;
			FILE* f = std::fopen(path.c_str(), "r");
			if (!f)
				return;
			char serial[128];
			Frame fr;
			char header[64];
			int version = 0;
			if (std::fscanf(f, "%63s %d", header, &version) == 2 && std::string(header) == "SpaceSyncLock" && version == 1)
			{
				while (std::fscanf(f, "%127s %lf %lf %lf %lf %lf %lf %lf", serial, &fr.q.w, &fr.q.x, &fr.q.y, &fr.q.z, &fr.t.v[0], &fr.t.v[1], &fr.t.v[2]) == 8)
				{
					fr.q = quaternionNormalize(fr.q);
					refs[serial] = fr;
				}
			}
			std::fclose(f);
			char buf[160];
			std::snprintf(buf, sizeof buf, "Lock Base Stations: loaded reference for %u base stations", (uint32_t)refs.size());
			say(buf);
		}

		void save()
		{
			dirty = false;
			FILE* f = std::fopen(path.c_str(), "w");
			if (!f)
				return;
			std::fprintf(f, "SpaceSyncLock 1\n");
			for (const auto& kv : refs)
				std::fprintf(f, "%s %.9f %.9f %.9f %.9f %.9f %.9f %.9f\n", kv.first.c_str(), kv.second.q.w, kv.second.q.x, kv.second.q.y, kv.second.q.z,
					kv.second.t.v[0], kv.second.t.v[1], kv.second.t.v[2]);
			std::fclose(f);
		}
	};
}
