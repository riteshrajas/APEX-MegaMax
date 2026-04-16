import test from 'node:test';
import assert from 'node:assert/strict';

function parseSmsPayload(payload) {
  let frame = payload.trim();
  if (frame.startsWith('ASP ')) {
    frame = frame.slice(4).trim();
  }

  if (frame.startsWith('{')) {
    const doc = JSON.parse(frame);
    return {
      id: doc.id ?? '',
      type: doc.type ?? '',
      command: doc.command ?? doc.query ?? '',
      requestedMode: doc.config?.mode ?? '',
      transport: doc.transport ?? ''
    };
  }

  return {
    id: '',
    type: 'command',
    command: frame,
    requestedMode: '',
    transport: 'sms'
  };
}

function updateState(mode, snapshot, ctx) {
  const idleExpired = ctx.nowMs - ctx.lastActivityMs >= ctx.sleepIdleMs;

  switch (mode) {
    case 'boot':
      return snapshot.modemReady ? 'connecting' : mode;
    case 'connecting':
      if (snapshot.networkRegistered && snapshot.dataAttached && snapshot.socketConnected) {
        return 'data';
      }
      if (snapshot.networkRegistered && snapshot.smsAvailable && ctx.dataFailures >= ctx.maxFailures) {
        return 'sms';
      }
      return mode;
    case 'data':
      if (!snapshot.socketConnected || !snapshot.dataAttached || ctx.dataFailures >= ctx.maxFailures) {
        return snapshot.smsAvailable ? 'sms' : 'connecting';
      }
      if (snapshot.idleEligible && idleExpired) {
        return 'sleep';
      }
      return mode;
    case 'sms':
      if (snapshot.networkRegistered && snapshot.dataAttached && snapshot.socketConnected) {
        return 'data';
      }
      if (snapshot.idleEligible && idleExpired) {
        return 'sleep';
      }
      return mode;
    case 'sleep':
      return snapshot.wakeRequested ? 'connecting' : mode;
    default:
      throw new Error(`Unknown mode ${mode}`);
  }
}

test('SMS parser accepts ASP JSON envelope', () => {
  const command = parseSmsPayload('ASP {"id":"42","type":"command","command":"SET_MODE","config":{"mode":"sleep"}}');
  assert.equal(command.id, '42');
  assert.equal(command.command, 'SET_MODE');
  assert.equal(command.requestedMode, 'sleep');
});

test('SMS parser accepts plain fail-over commands', () => {
  const command = parseSmsPayload('WAKE');
  assert.equal(command.command, 'WAKE');
  assert.equal(command.transport, 'sms');
});

test('state machine promotes healthy link to data mode', () => {
  const next = updateState('connecting', {
    modemReady: true,
    networkRegistered: true,
    dataAttached: true,
    socketConnected: true,
    smsAvailable: true,
    idleEligible: false,
    wakeRequested: false
  }, {
    nowMs: 1000,
    lastActivityMs: 0,
    sleepIdleMs: 120000,
    dataFailures: 0,
    maxFailures: 3
  });

  assert.equal(next, 'data');
});

test('state machine demotes to SMS after repeated data failures', () => {
  const next = updateState('data', {
    modemReady: true,
    networkRegistered: true,
    dataAttached: false,
    socketConnected: false,
    smsAvailable: true,
    idleEligible: false,
    wakeRequested: false
  }, {
    nowMs: 2000,
    lastActivityMs: 0,
    sleepIdleMs: 120000,
    dataFailures: 3,
    maxFailures: 3
  });

  assert.equal(next, 'sms');
});

test('state machine enters sleep only after idle expiry', () => {
  const next = updateState('sms', {
    modemReady: true,
    networkRegistered: true,
    dataAttached: false,
    socketConnected: false,
    smsAvailable: true,
    idleEligible: true,
    wakeRequested: false
  }, {
    nowMs: 125000,
    lastActivityMs: 0,
    sleepIdleMs: 120000,
    dataFailures: 3,
    maxFailures: 3
  });

  assert.equal(next, 'sleep');
});

test('state machine wakes back into connecting mode', () => {
  const next = updateState('sleep', {
    modemReady: true,
    networkRegistered: false,
    dataAttached: false,
    socketConnected: false,
    smsAvailable: true,
    idleEligible: false,
    wakeRequested: true
  }, {
    nowMs: 130000,
    lastActivityMs: 0,
    sleepIdleMs: 120000,
    dataFailures: 0,
    maxFailures: 3
  });

  assert.equal(next, 'connecting');
});
