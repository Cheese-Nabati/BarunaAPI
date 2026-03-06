const fastify = require('fastify')({ logger: true });

// Manual Route Register
fastify.post('/api/absen', async (request, reply) => {
  const { rfid_uid, device_id } = request.body;

  //UID Log
  console.log(`Log: Card UID : ${rfid_uid} Detected At : ${device_id}`);
  
  return { 
    status: 'success', 
    received_at: new Date().toISOString(),
    data: { rfid_uid, device_id }
  };
});

module.exports = fastify;